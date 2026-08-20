#include <shmipc/session.hpp>

#include "core/v2_multiplexed_session.hpp"
#include "transport/control_socket.hpp"
#include "transport/epoll_dispatcher.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
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
    try {
        shmipc::CallbackExecutor invalid_executor(0U);
        return false;
    } catch (const std::invalid_argument&) {
    } catch (...) {
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

struct ParallelState {
    std::mutex mutex{};
    std::condition_variable condition{};
    std::array<int, 2> active{};
    std::array<int, 2> maximum_active{};
    std::array<int, 2> data_count{};
    int entered_mask{0};
    int remote_close_count{0};
    bool release{false};
};

class ParallelCallbacks final : public shmipc::StreamCallbacks {
public:
    ParallelCallbacks(std::shared_ptr<ParallelState> value, std::size_t value_index)
        : state(std::move(value)), index(value_index) {}

    void on_data(shmipc::Stream&, std::vector<std::uint8_t>) override {
        std::unique_lock<std::mutex> lock(state->mutex);
        ++state->active[index];
        state->maximum_active[index] =
            std::max(state->maximum_active[index], state->active[index]);
        state->entered_mask |= 1 << static_cast<int>(index);
        state->condition.notify_all();
        static_cast<void>(state->condition.wait_for(
            lock, 5s, [&] { return state->release; }));
        --state->active[index];
        ++state->data_count[index];
        state->condition.notify_all();
    }

    void on_remote_close(shmipc::Stream&) override {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->remote_close_count;
        state->condition.notify_all();
    }

private:
    std::shared_ptr<ParallelState> state;
    std::size_t index;
};

struct BlockingState {
    std::mutex mutex{};
    std::condition_variable condition{};
    bool entered{false};
    bool release{false};
    bool local_close{false};
};

class BlockingCallbacks final : public shmipc::StreamCallbacks {
public:
    explicit BlockingCallbacks(std::shared_ptr<BlockingState> value)
        : state(std::move(value)) {}

    void on_data(shmipc::Stream&, std::vector<std::uint8_t>) override {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->entered = true;
        state->condition.notify_all();
        static_cast<void>(state->condition.wait_for(
            lock, 5s, [&] { return state->release; }));
    }

    void on_local_close(shmipc::Stream&) override {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->local_close = true;
        state->condition.notify_all();
    }

private:
    std::shared_ptr<BlockingState> state;
};

struct ThrowingState {
    std::mutex mutex{};
    std::condition_variable condition{};
    shmipc::Error error{shmipc::Error::none};
    bool local_close{false};
};

class ThrowingCallbacks final : public shmipc::StreamCallbacks {
public:
    explicit ThrowingCallbacks(std::shared_ptr<ThrowingState> value)
        : state(std::move(value)) {}

    void on_data(shmipc::Stream&, std::vector<std::uint8_t>) override {
        throw std::runtime_error("expected callback failure");
    }

    void on_error(shmipc::Stream&, const shmipc::Status& status) override {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->error = status.error;
        state->condition.notify_all();
    }

    void on_local_close(shmipc::Stream&) override {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->local_close = true;
        state->condition.notify_all();
    }

private:
    std::shared_ptr<ThrowingState> state;
};

class SelfClosingCallbacks final : public shmipc::StreamCallbacks {
public:
    explicit SelfClosingCallbacks(std::shared_ptr<BlockingState> value)
        : state(std::move(value)) {}

    void on_data(shmipc::Stream& stream,
                 std::vector<std::uint8_t>) override {
        const auto status = stream.close();
        std::lock_guard<std::mutex> lock(state->mutex);
        state->entered = static_cast<bool>(status);
        state->condition.notify_all();
    }

    void on_local_close(shmipc::Stream&) override {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->local_close = true;
        state->condition.notify_all();
    }

private:
    std::shared_ptr<BlockingState> state;
};

bool test_async_callbacks() {
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
    config.queue_name = directory.path + "/async-queue";
    config.buffer_name = directory.path + "/async-buffer";
    config.queue_capacity = 64U;
    config.buffer_size = 1U << 20U;
    config.buffer_tiers = {{4096U, 100U}};
    auto client = shmipc::connect_tcp("127.0.0.1", port.value, config);
    auto server = server_future.get();
    if (!client || !server) {
        return false;
    }
    auto executor = std::make_shared<shmipc::CallbackExecutor>(2U);

    std::array<shmipc::Stream, 2> client_streams{};
    std::array<shmipc::core::V2Stream, 2> server_streams{};
    std::array<shmipc::CallbackSubscription, 2> subscriptions{};
    auto parallel = std::make_shared<ParallelState>();
    for (std::size_t index = 0; index < client_streams.size(); ++index) {
        auto opened = client.value.open_stream();
        if (!opened || !opened.value.send(std::vector<std::uint8_t>{0x01U})) {
            return false;
        }
        auto accepted = server.value.accept_stream(5s);
        if (!accepted || !accepted.value.receive(5s)) {
            return false;
        }
        client_streams[index] = std::move(opened.value);
        server_streams[index] = std::move(accepted.value);
        auto registered = client_streams[index].set_callbacks(
            std::make_shared<ParallelCallbacks>(parallel, index), executor);
        if (!registered) {
            return false;
        }
        subscriptions[index] = std::move(registered.value);
        if (client_streams[index]
                .set_callbacks(
                    std::make_shared<ParallelCallbacks>(parallel, index),
                    executor)
                .status.error != shmipc::Error::callback_already_set) {
            return false;
        }
    }
    for (auto& stream : server_streams) {
        for (std::uint8_t value = 0U; value < 3U; ++value) {
            if (!stream.send(std::vector<std::uint8_t>{value})) {
                return false;
            }
        }
    }
    {
        std::unique_lock<std::mutex> lock(parallel->mutex);
        if (!parallel->condition.wait_for(
                lock, 5s, [&] { return parallel->entered_mask == 3; })) {
            return false;
        }
        parallel->release = true;
        parallel->condition.notify_all();
        if (!parallel->condition.wait_for(lock, 5s, [&] {
                return parallel->data_count[0] == 3 &&
                       parallel->data_count[1] == 3;
            }) ||
            parallel->maximum_active[0] != 1 ||
            parallel->maximum_active[1] != 1) {
            return false;
        }
    }
    for (auto& stream : server_streams) {
        if (!stream.close()) {
            return false;
        }
    }
    {
        std::unique_lock<std::mutex> lock(parallel->mutex);
        if (!parallel->condition.wait_for(
                lock, 5s, [&] { return parallel->remote_close_count == 2; })) {
            return false;
        }
    }

    auto blocking_opened = client.value.open_stream();
    if (!blocking_opened ||
        !blocking_opened.value.send(std::vector<std::uint8_t>{0x02U})) {
        return false;
    }
    auto blocking_server = server.value.accept_stream(5s);
    if (!blocking_server || !blocking_server.value.receive(5s)) {
        return false;
    }
    auto blocking = std::make_shared<BlockingState>();
    auto blocking_subscription = blocking_opened.value.set_callbacks(
        std::make_shared<BlockingCallbacks>(blocking), executor);
    if (!blocking_subscription ||
        !blocking_server.value.send(std::vector<std::uint8_t>{0x03U})) {
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(blocking->mutex);
        if (!blocking->condition.wait_for(lock, 5s,
                                          [&] { return blocking->entered; })) {
            return false;
        }
    }
    auto close_future = std::async(std::launch::async, [&] {
        return blocking_opened.value.close();
    });
    if (close_future.wait_for(100ms) != std::future_status::timeout) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(blocking->mutex);
        blocking->release = true;
        blocking->condition.notify_all();
    }
    if (close_future.wait_for(5s) != std::future_status::ready ||
        !close_future.get()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(blocking->mutex);
        if (!blocking->local_close) {
            return false;
        }
    }

    auto throwing_opened = client.value.open_stream();
    if (!throwing_opened ||
        !throwing_opened.value.send(std::vector<std::uint8_t>{0x04U})) {
        return false;
    }
    auto throwing_server = server.value.accept_stream(5s);
    if (!throwing_server || !throwing_server.value.receive(5s)) {
        return false;
    }
    auto throwing = std::make_shared<ThrowingState>();
    auto throwing_subscription = throwing_opened.value.set_callbacks(
        std::make_shared<ThrowingCallbacks>(throwing), executor);
    if (!throwing_subscription ||
        !throwing_server.value.send(std::vector<std::uint8_t>{0x05U})) {
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(throwing->mutex);
        if (!throwing->condition.wait_for(lock, 5s, [&] {
                return throwing->error == shmipc::Error::callback_error &&
                       throwing->local_close;
            })) {
            return false;
        }
    }

    auto self_closing_opened = client.value.open_stream();
    if (!self_closing_opened ||
        !self_closing_opened.value.send(std::vector<std::uint8_t>{0x06U})) {
        return false;
    }
    auto self_closing_server = server.value.accept_stream(5s);
    if (!self_closing_server || !self_closing_server.value.receive(5s)) {
        return false;
    }
    auto self_closing = std::make_shared<BlockingState>();
    auto self_closing_subscription = self_closing_opened.value.set_callbacks(
        std::make_shared<SelfClosingCallbacks>(self_closing), executor);
    if (!self_closing_subscription ||
        !self_closing_server.value.send(std::vector<std::uint8_t>{0x07U})) {
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(self_closing->mutex);
        if (!self_closing->condition.wait_for(lock, 5s, [&] {
                return self_closing->entered && self_closing->local_close;
            })) {
            return false;
        }
    }

    for (auto& subscription : subscriptions) {
        if (!subscription.stop()) {
            return false;
        }
    }
    if (!blocking_subscription.value.stop() ||
        !throwing_subscription.value.stop() ||
        !self_closing_subscription.value.stop() ||
        !blocking_server.value.wait_remote_close(5s) ||
        !throwing_server.value.wait_remote_close(5s) ||
        !self_closing_server.value.wait_remote_close(5s) ||
        !blocking_server.value.close() || !throwing_server.value.close() ||
        !self_closing_server.value.close() ||
        !client.value.close() || !server.value.close() ||
        dispatcher.value.stop() != shmipc::transport::TransportError::none) {
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!test_error_surface() || !test_file_mode_tcp() ||
        !test_memfd_mode_unix() || !test_async_callbacks()) {
        std::cerr << "public session test failed\n";
        return 1;
    }
    return 0;
}
