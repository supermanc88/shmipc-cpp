#include <shmipc/listener.hpp>
#include <shmipc/stream_connection.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct TestDirectory {
    std::array<char, 64> storage{};
    std::string path{};

    bool create() {
        const std::string pattern = "/tmp/shmipc-listener.XXXXXX";
        std::copy(pattern.begin(), pattern.end(), storage.begin());
        auto* const directory = ::mkdtemp(storage.data());
        if (directory == nullptr) {
            return false;
        }
        path = directory;
        return true;
    }

    ~TestDirectory() {
        if (!path.empty()) {
            static_cast<void>(::rmdir(path.c_str()));
        }
    }
};

bool read_exact(shmipc::StreamConnection& connection,
                std::vector<std::uint8_t>& output,
                std::chrono::milliseconds timeout) {
    std::size_t offset = 0U;
    while (offset < output.size()) {
        const auto result = connection.read(output.data() + offset,
                                            output.size() - offset, timeout);
        if (!result || result.transferred == 0U) {
            return false;
        }
        offset += result.transferred;
    }
    return true;
}

bool exercise_listener(shmipc::Listener& listener,
                       shmipc::SessionResult client,
                       shmipc::SessionResult accepted_session) {
    if (!client || !accepted_session) {
        return false;
    }
    auto opened = client.value.open_stream();
    if (!opened ||
        client.value.accept_stream(0ms).status.error !=
            shmipc::Error::unsupported) {
        return false;
    }
    shmipc::StreamConnection client_connection(std::move(opened.value));
    const std::array<std::uint8_t, 3> first{{'a', 'b', 'c'}};
    if (!client_connection.write(first.data(), first.size())) {
        return false;
    }

    if (accepted_session.value.open_stream().status.error !=
            shmipc::Error::unsupported) {
        return false;
    }
    auto accepted_stream = accepted_session.value.accept_stream(5s);
    if (!accepted_stream) {
        return false;
    }
    shmipc::StreamConnection server_connection(
        std::move(accepted_stream.value));
    const std::array<std::uint8_t, 4> second{{'d', 'e', 'f', 'g'}};
    if (!client_connection.write(second.data(), second.size())) {
        return false;
    }

    std::array<std::uint8_t, 2> prefix{};
    auto prefix_read = server_connection.read(prefix.data(), prefix.size(), 5s);
    if (!prefix_read || prefix_read.transferred != prefix.size() ||
        prefix[0] != 'a' || prefix[1] != 'b') {
        return false;
    }
    std::vector<std::uint8_t> remainder(5U);
    if (!read_exact(server_connection, remainder, 5s) ||
        remainder != std::vector<std::uint8_t>({'c', 'd', 'e', 'f', 'g'})) {
        return false;
    }

    if (!listener.close() || listener ||
        listener.accept_session(0ms).status.error != shmipc::Error::closed) {
        return false;
    }
    const std::array<std::uint8_t, 5> response{{'r', 'e', 'p', 'l', 'y'}};
    if (!server_connection.write(response.data(), response.size())) {
        return false;
    }
    std::vector<std::uint8_t> observed(response.size());
    if (!read_exact(client_connection, observed, 5s) ||
        !std::equal(observed.begin(), observed.end(), response.begin())) {
        return false;
    }
    return client_connection.close() && server_connection.close() &&
           client.value.close() && accepted_session.value.close();
}

bool test_tcp_file_listener() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    auto listener = shmipc::listen_tcp("127.0.0.1", 0U);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    if (listener.value.local_port() == 0U ||
        listener.value.accept_session(0ms).status.error !=
            shmipc::Error::timeout) {
        return false;
    }
    shmipc::ClientConfig config;
    config.queue_name = directory.path + "/queue";
    config.buffer_name = directory.path + "/buffer";
    config.queue_capacity = 64U;
    config.buffer_size = 1U << 20U;
    config.buffer_tiers = {{4096U, 100U}};
    auto client_future = std::async(std::launch::async, [&] {
        return shmipc::connect_tcp("127.0.0.1", listener.value.local_port(),
                                   config);
    });
    auto accepted_future = std::async(std::launch::async, [&] {
        return listener.value.accept_session(5s);
    });
    auto accepted = accepted_future.get();
    auto client = client_future.get();
    if (!accepted || !client) {
        return false;
    }

    auto opened = client.value.open_stream();
    if (!opened || !opened.value.send(std::vector<std::uint8_t>{0x42U})) {
        return false;
    }
    auto stream = accepted.value.accept_stream(5s);
    if (!stream || !stream.value.receive(5s) || !listener.value.close()) {
        return false;
    }
    if (!stream.value.send(std::vector<std::uint8_t>{0x24U})) {
        return false;
    }
    auto response = opened.value.receive(5s);
    return response && response.value == std::vector<std::uint8_t>{0x24U} &&
           opened.value.close() && stream.value.close() &&
           client.value.close() && accepted.value.close();
}

bool test_unix_memfd_listener() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    const auto socket_path = directory.path + "/control.sock";
    shmipc::ListenerConfig listener_config;
    listener_config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    auto listener = shmipc::listen_unix(socket_path, listener_config);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    shmipc::ClientConfig config;
    config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    config.queue_name = "public-listener-queue";
    config.buffer_name = "public-listener-buffer";
    config.queue_capacity = 64U;
    config.buffer_size = 1U << 20U;
    config.buffer_tiers = {{4096U, 100U}};
    auto client_future = std::async(std::launch::async, [&] {
        return shmipc::connect_unix(socket_path, config);
    });
    auto accepted = listener.value.accept_session(5s);
    auto client = client_future.get();
    return exercise_listener(listener.value, std::move(client),
                             std::move(accepted));
}

bool test_close_unblocks_accept() {
    auto listener = shmipc::listen_tcp("127.0.0.1", 0U);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    auto accepted = std::async(std::launch::async, [&] {
        return listener.value.accept_session(5s);
    });
    std::this_thread::sleep_for(20ms);
    if (!listener.value.close() ||
        accepted.wait_for(1s) != std::future_status::ready) {
        return false;
    }
    return accepted.get().status.error == shmipc::Error::closed;
}

bool test_move_assignment_closes_previous_listener() {
    auto first = shmipc::listen_tcp("127.0.0.1", 0U);
    if (!first) {
        return first.status.error == shmipc::Error::unsupported;
    }
    auto second = shmipc::listen_tcp("127.0.0.1", 0U);
    if (!second) {
        return false;
    }
    auto accepted = std::async(std::launch::async, [&] {
        return first.value.accept_session(5s);
    });
    std::this_thread::sleep_for(20ms);
    first.value = std::move(second.value);
    if (!first.value || second.value ||
        accepted.wait_for(1s) != std::future_status::ready ||
        accepted.get().status.error != shmipc::Error::closed) {
        return false;
    }
    return static_cast<bool>(first.value.close());
}

bool test_invalid_listener_config() {
    shmipc::ListenerConfig invalid;
    invalid.backlog = 0;
    if (shmipc::listen_tcp("127.0.0.1", 0U, invalid).status.error !=
        shmipc::Error::invalid_argument) {
        return false;
    }
    invalid.backlog = 1;
    invalid.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    return shmipc::listen_tcp("127.0.0.1", 0U, invalid).status.error ==
           shmipc::Error::unsupported;
}

}  // namespace

int main() {
    if (!test_invalid_listener_config() || !test_tcp_file_listener() ||
        !test_unix_memfd_listener() || !test_close_unblocks_accept() ||
        !test_move_assignment_closes_previous_listener()) {
        std::cerr << "public listener test failed\n";
        return 1;
    }
    return 0;
}
