#include <shmipc/listener.hpp>
#include <shmipc/session_manager.hpp>

#include <algorithm>
#include <array>
#include <atomic>
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
        const std::string pattern = "/tmp/shmipc-manager.XXXXXX";
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

shmipc::SessionManagerConfig memfd_manager_config() {
    shmipc::SessionManagerConfig config;
    config.client_config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    config.client_config.queue_name = "public-manager-queue";
    config.client_config.buffer_name = "public-manager-buffer";
    config.client_config.queue_capacity = 64U;
    config.client_config.buffer_size = 1U << 20U;
    config.client_config.buffer_tiers = {{4096U, 100U}};
    config.reconnect_interval = 20ms;
    config.health_check_interval = 10ms;
    return config;
}

bool receive_equals(shmipc::Stream& stream,
                    const std::vector<std::uint8_t>& expected) {
    const auto received = stream.receive(5s);
    return received && received.value == expected;
}

bool test_round_robin_and_reuse() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    shmipc::ListenerConfig listener_config;
    listener_config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    auto listener =
        shmipc::listen_unix(directory.path + "/control.sock", listener_config);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    auto config = memfd_manager_config();
    config.session_count = 2U;
    config.round_robin_batch = 2U;
    config.max_idle_streams_per_session = 2U;
    auto manager_future = std::async(std::launch::async, [&] {
        return shmipc::make_unix_session_manager(
            directory.path + "/control.sock", config);
    });
    std::array<shmipc::Session, 2> servers{};
    for (auto& server : servers) {
        auto accepted = listener.value.accept_session(5s);
        if (!accepted) {
            return false;
        }
        server = std::move(accepted.value);
    }
    auto manager = manager_future.get();
    if (!manager || manager.value.session_count() != 2U) {
        return false;
    }

    const std::vector<std::uint8_t> first{1U};
    auto lease0 = manager.value.get_stream();
    if (!lease0 || lease0.value.session_index() != 0U ||
        !lease0.value.stream().send(first)) {
        return false;
    }
    const auto id0 = lease0.value.stream().id();
    auto server_stream0 = servers[0].accept_stream(5s);
    if (!server_stream0 || !receive_equals(server_stream0.value, first) ||
        !lease0.value.return_to_pool()) {
        return false;
    }

    const std::vector<std::uint8_t> second{2U};
    auto lease1 = manager.value.get_stream();
    if (!lease1 || lease1.value.session_index() != 1U ||
        !lease1.value.stream().send(second)) {
        return false;
    }
    const auto id1 = lease1.value.stream().id();
    auto server_stream1 = servers[1].accept_stream(5s);
    if (!server_stream1 || !receive_equals(server_stream1.value, second) ||
        !lease1.value.return_to_pool()) {
        return false;
    }

    const std::vector<std::uint8_t> third{3U};
    auto reused1 = manager.value.get_stream();
    if (!reused1 || reused1.value.session_index() != 1U ||
        reused1.value.stream().id() != id1 ||
        !reused1.value.stream().send(third) ||
        !receive_equals(server_stream1.value, third) ||
        !reused1.value.return_to_pool()) {
        return false;
    }
    const std::vector<std::uint8_t> fourth{4U};
    auto reused0 = manager.value.get_stream();
    if (!reused0 || reused0.value.session_index() != 0U ||
        reused0.value.stream().id() != id0 ||
        !reused0.value.stream().send(fourth) ||
        !receive_equals(server_stream0.value, fourth) ||
        !reused0.value.return_to_pool()) {
        return false;
    }

    static_cast<void>(manager.value.close());
    static_cast<void>(server_stream0.value.close());
    static_cast<void>(server_stream1.value.close());
    static_cast<void>(servers[0].close());
    static_cast<void>(servers[1].close());
    static_cast<void>(listener.value.close());
    return true;
}

bool test_pool_capacity_closes_excess() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    shmipc::ListenerConfig listener_config;
    listener_config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    auto listener =
        shmipc::listen_unix(directory.path + "/control.sock", listener_config);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    auto config = memfd_manager_config();
    config.max_idle_streams_per_session = 1U;
    auto manager_future = std::async(std::launch::async, [&] {
        return shmipc::make_unix_session_manager(
            directory.path + "/control.sock", config);
    });
    auto accepted = listener.value.accept_session(5s);
    auto manager = manager_future.get();
    if (!accepted || !manager) {
        return false;
    }
    auto first = manager.value.get_stream();
    auto second = manager.value.get_stream();
    if (!first || !second || first.value.stream().id() == second.value.stream().id() ||
        !first.value.stream().send(std::vector<std::uint8_t>{1U}) ||
        !second.value.stream().send(std::vector<std::uint8_t>{2U})) {
        return false;
    }
    const auto first_id = first.value.stream().id();
    auto server_first = accepted.value.accept_stream(5s);
    auto server_second = accepted.value.accept_stream(5s);
    if (!server_first || !server_second ||
        !receive_equals(server_first.value, std::vector<std::uint8_t>{1U}) ||
        !receive_equals(server_second.value, std::vector<std::uint8_t>{2U}) ||
        !first.value.return_to_pool() || !second.value.return_to_pool() ||
        !server_second.value.wait_remote_close(5s)) {
        return false;
    }
    auto reused = manager.value.get_stream();
    const auto reused_first = reused && reused.value.stream().id() == first_id;
    static_cast<void>(reused.value.close());
    static_cast<void>(manager.value.close());
    static_cast<void>(server_first.value.close());
    static_cast<void>(server_second.value.close());
    static_cast<void>(accepted.value.close());
    static_cast<void>(listener.value.close());
    return reused_first;
}

bool test_reconnect_after_remote_close() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    auto listener = shmipc::listen_tcp("127.0.0.1", 0U);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    shmipc::SessionManagerConfig config;
    config.client_config.queue_name = directory.path + "/queue";
    config.client_config.buffer_name = directory.path + "/buffer";
    config.client_config.queue_capacity = 64U;
    config.client_config.buffer_size = 1U << 20U;
    config.client_config.buffer_tiers = {{4096U, 100U}};
    config.reconnect_interval = 20ms;
    config.health_check_interval = 10ms;
    auto manager_future = std::async(std::launch::async, [&] {
        return shmipc::make_tcp_session_manager(
            "127.0.0.1", listener.value.local_port(), config);
    });
    auto first_server = listener.value.accept_session(5s);
    auto manager = manager_future.get();
    if (!first_server || !manager) {
        return false;
    }
    auto first_lease = manager.value.get_stream();
    if (!first_lease ||
        !first_lease.value.stream().send(std::vector<std::uint8_t>{1U})) {
        return false;
    }
    auto first_stream = first_server.value.accept_stream(5s);
    if (!first_stream ||
        !receive_equals(first_stream.value, std::vector<std::uint8_t>{1U}) ||
        !first_lease.value.return_to_pool() || !first_server.value.close()) {
        return false;
    }

    auto reaccepted = std::async(std::launch::async, [&] {
        return listener.value.accept_session(3s);
    });
    if (reaccepted.wait_for(4s) != std::future_status::ready) {
        return false;
    }
    auto second_server = reaccepted.get();
    if (!second_server) {
        return false;
    }
    shmipc::PooledStreamResult second_lease;
    bool acquired = false;
    for (std::size_t attempt = 0U; attempt < 100U && !acquired; ++attempt) {
        second_lease = manager.value.get_stream();
        acquired = static_cast<bool>(second_lease);
        if (!acquired) {
            std::this_thread::sleep_for(10ms);
        }
    }
    if (!acquired ||
        !second_lease.value.stream().send(std::vector<std::uint8_t>{2U})) {
        return false;
    }
    auto second_stream = second_server.value.accept_stream(5s);
    const auto received =
        second_stream &&
        receive_equals(second_stream.value, std::vector<std::uint8_t>{2U});
    static_cast<void>(second_lease.value.close());
    static_cast<void>(manager.value.close());
    static_cast<void>(first_stream.value.close());
    static_cast<void>(second_stream.value.close());
    static_cast<void>(second_server.value.close());
    static_cast<void>(listener.value.close());
    return received;
}

bool test_concurrent_close_and_get() {
    TestDirectory directory;
    if (!directory.create()) {
        return false;
    }
    shmipc::ListenerConfig listener_config;
    listener_config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    auto listener =
        shmipc::listen_unix(directory.path + "/control.sock", listener_config);
    if (!listener) {
        return listener.status.error == shmipc::Error::unsupported;
    }
    auto config = memfd_manager_config();
    auto manager_future = std::async(std::launch::async, [&] {
        return shmipc::make_unix_session_manager(
            directory.path + "/control.sock", config);
    });
    auto accepted = listener.value.accept_session(5s);
    auto manager = manager_future.get();
    if (!accepted || !manager) {
        return false;
    }

    constexpr std::size_t worker_count = 4U;
    std::atomic<std::size_t> ready{0U};
    std::atomic<bool> start{false};
    std::atomic<bool> unexpected_error{false};
    std::array<std::thread, worker_count> workers;
    for (auto& worker : workers) {
        worker = std::thread([&] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t attempt = 0U; attempt < 1000U; ++attempt) {
                auto lease = manager.value.get_stream();
                if (!lease) {
                    if (lease.status.error != shmipc::Error::closed) {
                        unexpected_error.store(true, std::memory_order_release);
                    }
                    return;
                }
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != worker_count) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    std::this_thread::sleep_for(2ms);
    static_cast<void>(manager.value.close());
    for (auto& worker : workers) {
        worker.join();
    }
    const auto after_close = manager.value.get_stream();
    static_cast<void>(accepted.value.close());
    static_cast<void>(listener.value.close());
    return !unexpected_error.load(std::memory_order_acquire) && !after_close &&
           after_close.status.error == shmipc::Error::closed;
}

bool test_invalid_config() {
    shmipc::SessionManagerConfig config;
    config.session_count = 0U;
    if (shmipc::make_tcp_session_manager("127.0.0.1", 1U, config)
            .status.error != shmipc::Error::invalid_argument) {
        return false;
    }
    config.session_count = 1U;
    config.round_robin_batch = 0U;
    if (shmipc::make_unix_session_manager("/tmp/unused", config)
            .status.error != shmipc::Error::invalid_argument) {
        return false;
    }
    config.round_robin_batch = 1U;
    config.client_config.shared_memory_mode = shmipc::SharedMemoryMode::memfd;
    return shmipc::make_tcp_session_manager("127.0.0.1", 1U, config)
               .status.error == shmipc::Error::unsupported;
}

}  // namespace

int main() {
    if (!test_invalid_config() || !test_round_robin_and_reuse() ||
        !test_pool_capacity_closes_excess() ||
        !test_reconnect_after_remote_close() ||
        !test_concurrent_close_and_get()) {
        std::cerr << "public session manager test failed\n";
        return 1;
    }
    return 0;
}
