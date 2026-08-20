#include <shmipc/session.hpp>

#include "core/v2_multiplexed_session.hpp"
#include "transport/control_socket.hpp"
#include "transport/epoll_dispatcher.hpp"

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
        const std::string pattern = "/tmp/shmipc-public.XXXXXX";
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

bool exercise_stream(shmipc::Session& client,
                     shmipc::core::V2MultiplexedServerSession& server) {
    auto opened = client.open_stream();
    if (!opened || opened.value.id() != 2U || !client.is_open() ||
        !client.is_healthy()) {
        return false;
    }
    const std::vector<std::uint8_t> request{0x10U, 0x20U, 0x30U, 0x40U};
    if (!opened.value.send(request)) {
        return false;
    }

    auto accepted = server.accept_stream(5s);
    if (!accepted) {
        return false;
    }
    auto received = accepted.value.receive(5s);
    if (!received || received.value != request ||
        !accepted.value.send(received.value)) {
        return false;
    }
    auto echoed = opened.value.receive(5s);
    if (!echoed || echoed.value != request || opened.value.is_fallback()) {
        return false;
    }
    if (!opened.value.close() || !accepted.value.wait_remote_close(5s) ||
        !accepted.value.close()) {
        return false;
    }
    return true;
}

bool test_file_mode_tcp() {
    auto listener = shmipc::transport::listen_tcp("127.0.0.1", 0U);
    auto port = listener ? listener.value.local_port()
                         : shmipc::transport::TransportResult<std::uint16_t>{};
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    TestDirectory directory;
    if (!dispatcher) {
        return dispatcher.error == shmipc::transport::TransportError::unsupported;
    }
    if (!listener || !port || !directory.create()) {
        return false;
    }

    auto server_future = std::async(std::launch::async, [&] {
        auto socket = listener.value.accept();
        if (!socket) {
            return shmipc::core::V2MultiplexedServerSessionResult{};
        }
        return shmipc::core::start_v2_multiplexed_server_session(
            std::move(socket.value), dispatcher.value);
    });

    shmipc::ClientConfig config;
    config.queue_name = directory.path + "/queue";
    config.buffer_name = directory.path + "/buffer";
    config.queue_capacity = 64U;
    config.buffer_size = 1U << 20U;
    config.buffer_tiers = {{4096U, 60U}, {8192U, 40U}};
    auto client = shmipc::connect_tcp("127.0.0.1", port.value, config);
    auto server = server_future.get();
    if (!client || !server || !exercise_stream(client.value, server.value)) {
        return false;
    }
    if (!client.value.close() || !server.value.close() ||
        dispatcher.value.stop() != shmipc::transport::TransportError::none) {
        return false;
    }
    return ::access(config.queue_name.c_str(), F_OK) != 0 &&
           ::access(config.buffer_name.c_str(), F_OK) != 0;
}

bool test_memfd_mode_unix() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    const auto socket_path = directory.path + "/control.sock";
    auto listener = shmipc::transport::listen_unix(socket_path);
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!dispatcher) {
        return dispatcher.error == shmipc::transport::TransportError::unsupported;
    }
    if (!listener) {
        return false;
    }

    auto server_future = std::async(std::launch::async, [&] {
        auto socket = listener.value.accept();
        if (!socket) {
            return shmipc::core::V3MultiplexedServerSessionResult{};
        }
        return shmipc::core::start_v3_multiplexed_server_session(
            std::move(socket.value), dispatcher.value);
    });

    shmipc::ClientConfig config;
    config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    config.queue_name = "shmipc-public-queue";
    config.buffer_name = "shmipc-public-buffer";
    config.queue_capacity = 64U;
    config.buffer_size = 1U << 20U;
    config.buffer_tiers = {{4096U, 100U}};
    auto client = shmipc::connect_unix(socket_path, config);
    auto server = server_future.get();
    if (!client || !server || !exercise_stream(client.value, server.value)) {
        return false;
    }
    return client.value.close() && server.value.close() &&
           dispatcher.value.stop() == shmipc::transport::TransportError::none;
}

bool test_error_surface() {
    shmipc::Session session;
    shmipc::Stream stream;
    if (session || session.is_open() || session.is_healthy() || stream ||
        stream.id() != 0U || stream.is_fallback() || !session.close() ||
        !stream.close() ||
        session.open_stream().status.error != shmipc::Error::closed ||
        stream.receive(1ms).status.error != shmipc::Error::closed ||
        std::string(shmipc::to_string(shmipc::Error::unhealthy)) !=
            "unhealthy") {
        return false;
    }

    shmipc::ClientConfig invalid;
    invalid.buffer_tiers = {{4096U, 99U}};
    if (shmipc::connect_unix("/not-used", invalid).status.error !=
        shmipc::Error::invalid_argument) {
        return false;
    }
    invalid.buffer_tiers = {{4096U, 50U}, {4096U, 50U}};
    if (shmipc::connect_unix("/not-used", invalid).status.error !=
        shmipc::Error::invalid_argument) {
        return false;
    }
    shmipc::ClientConfig memfd;
    memfd.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    return shmipc::connect_tcp("127.0.0.1", 1U, memfd).status.error ==
           shmipc::Error::unsupported;
}

}  // namespace

int main() {
    if (!test_error_surface() || !test_file_mode_tcp() ||
        !test_memfd_mode_unix()) {
        std::cerr << "public session test failed\n";
        return 1;
    }
    return 0;
}
