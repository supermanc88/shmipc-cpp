#include "transport/control_socket.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

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

bool test_move_and_errors() {
    auto invalid = shmipc::transport::adopt_control_socket(-1);
    shmipc::transport::ControlSocket empty;
    std::uint8_t byte = 0;
    return !invalid &&
           invalid.error == TransportError::invalid_argument &&
           empty.read_full(&byte, 1U).error == TransportError::invalid_state &&
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
    if (!test_move_and_errors()) {
        std::cerr << "control socket ownership/error test failed\n";
        return 1;
    }
    return 0;
}
