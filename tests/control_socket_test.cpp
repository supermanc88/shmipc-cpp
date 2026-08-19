#include "transport/control_socket.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using shmipc::transport::TransportError;

bool test_exact_io_and_eof() {
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return false;
    }
    auto left = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto right = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!left || !right) {
        return false;
    }
    const std::array<std::uint8_t, 6> expected{{1U, 2U, 3U, 4U, 5U, 6U}};
    std::thread writer([&] {
        const auto first = right.value.write_full(expected.data(), 2U);
        const auto second = right.value.write_full(expected.data() + 2U, 4U);
        if (!first || !second) {
            static_cast<void>(right.value.close());
            return;
        }
        static_cast<void>(right.value.shutdown());
    });
    std::array<std::uint8_t, 8> received{};
    const auto full = left.value.read_full(received.data(), expected.size());
    const auto eof = left.value.read_full(received.data() + expected.size(), 2U);
    writer.join();
    return full && full.value.transferred == expected.size() &&
           std::memcmp(received.data(), expected.data(), expected.size()) == 0 &&
           eof.error == TransportError::end_of_stream &&
           eof.value.transferred == 0U;
}

bool test_nonblocking_progress() {
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return false;
    }
    auto left = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto right = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!left || !right ||
        left.value.set_nonblocking(true) != TransportError::none) {
        return false;
    }
    std::array<std::uint8_t, 4> received{};
    const auto empty = left.value.read_full(received.data(), received.size());
    const std::array<std::uint8_t, 2> partial{{9U, 8U}};
    const auto sent = right.value.write_full(partial.data(), partial.size());
    const auto read = left.value.read_full(received.data(), received.size());
    return empty.error == TransportError::would_block &&
           empty.value.transferred == 0U && sent &&
           read.error == TransportError::would_block &&
           read.value.transferred == partial.size() && received[0] == 9U &&
           received[1] == 8U;
}

bool exchange(shmipc::transport::ControlListener& listener,
              shmipc::transport::ControlSocket& client) {
    bool server_ok = false;
    std::thread server([&] {
        auto accepted = listener.accept();
        std::array<std::uint8_t, 4> request{};
        const auto read = accepted
                              ? accepted.value.read_full(request.data(),
                                                         request.size())
                              : shmipc::transport::IoResult{};
        const std::array<std::uint8_t, 4> expected{{'p', 'i', 'n', 'g'}};
        const std::array<std::uint8_t, 4> response{{'p', 'o', 'n', 'g'}};
        const auto write = read && request == expected
                               ? accepted.value.write_full(response.data(),
                                                           response.size())
                               : shmipc::transport::IoResult{};
        server_ok = accepted && read && write;
    });
    const std::array<std::uint8_t, 4> request{{'p', 'i', 'n', 'g'}};
    const std::array<std::uint8_t, 4> expected{{'p', 'o', 'n', 'g'}};
    std::array<std::uint8_t, 4> response{};
    const auto write = client.write_full(request.data(), request.size());
    const auto read = write
                          ? client.read_full(response.data(), response.size())
                          : shmipc::transport::IoResult{};
    server.join();
    return server_ok && read && response == expected;
}

bool test_tcp_transport() {
    auto listener = shmipc::transport::listen_tcp("127.0.0.1", 0U);
    const auto port = listener ? listener.value.local_port()
                               : shmipc::transport::TransportResult<std::uint16_t>{};
    auto client = port ? shmipc::transport::connect_tcp("127.0.0.1", port.value)
                       : shmipc::transport::ControlSocketResult{};
    return listener && port && port.value != 0U && client &&
           exchange(listener.value, client.value);
}

bool test_unix_transport_and_cleanup() {
    std::string path = "/tmp/shmipc-control-XXXXXX";
    std::array<char, 64> directory{};
    std::memcpy(directory.data(), path.c_str(), path.size() + 1U);
    auto* const created = ::mkdtemp(directory.data());
    if (created == nullptr) {
        return false;
    }
    path = std::string(created) + "/socket";
    bool result = false;
    {
        auto listener = shmipc::transport::listen_unix(path);
        auto duplicate = listener
                             ? shmipc::transport::listen_unix(path)
                             : shmipc::transport::ControlListenerResult{};
        auto client = listener ? shmipc::transport::connect_unix(path)
                               : shmipc::transport::ControlSocketResult{};
        result = listener && !duplicate && client &&
                 exchange(listener.value, client.value) &&
                 ::access(path.c_str(), F_OK) == 0;
    }
    const auto cleaned = ::access(path.c_str(), F_OK) != 0;
    static_cast<void>(::rmdir(created));
    return result && cleaned;
}

bool test_file_descriptor_transfer() {
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return false;
    }
    auto sender = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto receiver = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!sender || !receiver) {
        return false;
    }
#if defined(__linux__)
    int first_pipe[2] = {-1, -1};
    int second_pipe[2] = {-1, -1};
    if (::pipe(first_pipe) != 0 || ::pipe(second_pipe) != 0) {
        return false;
    }
    const std::array<int, 2> sent_descriptors{{first_pipe[0], second_pipe[1]}};
    const auto sent = sender.value.send_file_descriptors(
        sent_descriptors.data(), sent_descriptors.size());
    auto received = receiver.value.receive_file_descriptors(2U);
    if (!sent || sent.value.transferred != 2U || !received ||
        received.value.size() != 2U || received.value.at(0U) < 0 ||
        received.value.at(1U) < 0 ||
        (::fcntl(received.value.at(0U), F_GETFD) & FD_CLOEXEC) == 0 ||
        (::fcntl(received.value.at(1U), F_GETFD) & FD_CLOEXEC) == 0) {
        return false;
    }
    const std::uint8_t first_value = 0x41U;
    const std::uint8_t second_value = 0x52U;
    std::uint8_t observed = 0U;
    if (::write(first_pipe[1], &first_value, 1U) != 1 ||
        ::read(received.value.at(0U), &observed, 1U) != 1 ||
        observed != first_value ||
        ::write(received.value.at(1U), &second_value, 1U) != 1 ||
        ::read(second_pipe[0], &observed, 1U) != 1 ||
        observed != second_value) {
        return false;
    }
    const auto released = received.value.release(0U);
    const auto retained = received.value.at(1U);
    received = {};
    errno = 0;
    const bool retained_closed =
        ::fcntl(retained, F_GETFD) == -1 && errno == EBADF;
    const bool released_open = ::fcntl(released, F_GETFD) >= 0;
    static_cast<void>(::close(released));
    static_cast<void>(::close(first_pipe[0]));
    static_cast<void>(::close(first_pipe[1]));
    static_cast<void>(::close(second_pipe[0]));
    static_cast<void>(::close(second_pipe[1]));
    return retained_closed && released_open;
#else
    const int descriptor = 0;
    return sender.value.send_file_descriptors(&descriptor, 1U).error ==
               TransportError::unsupported &&
           receiver.value.receive_file_descriptors(1U).error ==
               TransportError::unsupported;
#endif
}

bool test_file_descriptor_count_limits() {
#if defined(__linux__)
    int descriptors[2] = {-1, -1};
    int pipe_descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0 ||
        ::pipe(pipe_descriptors) != 0) {
        return false;
    }
    auto sender = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto receiver = shmipc::transport::adopt_control_socket(descriptors[1]);
    const std::array<int, 3> too_many{
        {pipe_descriptors[0], pipe_descriptors[0], pipe_descriptors[0]}};
    const auto sent =
        sender.value.send_file_descriptors(too_many.data(), too_many.size());
    auto received = receiver.value.receive_file_descriptors(2U);
    static_cast<void>(::close(pipe_descriptors[0]));
    static_cast<void>(::close(pipe_descriptors[1]));
    return sent && received.error == TransportError::buffer_limit;
#else
    return true;
#endif
}

bool test_move_and_errors() {
    auto invalid = shmipc::transport::adopt_control_socket(-1);
    shmipc::transport::ControlSocket empty;
    std::uint8_t byte = 0;
    return !invalid &&
           invalid.error == TransportError::invalid_argument &&
           empty.read_full(&byte, 1U).error == TransportError::invalid_state &&
           empty.send_file_descriptors(nullptr, 0U).error ==
               TransportError::invalid_state &&
           shmipc::transport::connect_tcp("", 1U).error ==
               TransportError::invalid_argument &&
           shmipc::transport::listen_unix("", 1).error ==
               TransportError::invalid_argument;
}

}  // namespace

int main() {
    if (!test_exact_io_and_eof()) {
        std::cerr << "control socket exact IO/EOF test failed\n";
        return 1;
    }
    if (!test_nonblocking_progress()) {
        std::cerr << "control socket nonblocking progress test failed\n";
        return 1;
    }
    if (!test_tcp_transport()) {
        std::cerr << "control socket TCP test failed\n";
        return 1;
    }
    if (!test_unix_transport_and_cleanup()) {
        std::cerr << "control socket Unix test failed\n";
        return 1;
    }
    if (!test_file_descriptor_transfer()) {
        std::cerr << "control socket FD transfer test failed\n";
        return 1;
    }
    if (!test_file_descriptor_count_limits()) {
        std::cerr << "control socket FD count-limit test failed\n";
        return 1;
    }
    if (!test_move_and_errors()) {
        std::cerr << "control socket ownership/error test failed\n";
        return 1;
    }
    return 0;
}
