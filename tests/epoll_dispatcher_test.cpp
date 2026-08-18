#include "transport/epoll_dispatcher.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>

namespace {

using shmipc::transport::ConnectionCloseReason;
using shmipc::transport::ConsumeResult;
using shmipc::transport::ControlEventCallback;
using shmipc::transport::EventConnection;
using shmipc::transport::TransportError;

#ifdef __linux__

class RecordingCallback final : public ControlEventCallback {
public:
    explicit RecordingCallback(std::size_t expected_size,
                               bool invalid_consume = false,
                               bool close_on_data = false)
        : expected_size_(expected_size),
          invalid_consume_(invalid_consume),
          close_on_data_(close_on_data) {}

    ConsumeResult on_data(const std::uint8_t* data, std::size_t size,
                          EventConnection& connection) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++data_calls_;
        if (invalid_consume_) {
            condition_.notify_all();
            return {size + 1U, TransportError::none, 0};
        }
        if (size >= expected_size_) {
            data_.assign(data, data + expected_size_);
            if (close_on_data_) {
                static_cast<void>(connection.close());
            }
            condition_.notify_all();
            return {expected_size_, TransportError::none, 0};
        }
        condition_.notify_all();
        return {};
    }

    void on_close(ConnectionCloseReason reason, int system_error) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++close_calls_;
        close_reason_ = reason;
        close_error_ = system_error;
        condition_.notify_all();
    }

    bool wait_for_data_calls(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(5),
                                   [&] { return data_calls_ >= count; });
    }

    bool wait_for_data() {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(5),
                                   [&] { return data_.size() == expected_size_; });
    }

    bool wait_for_close(ConnectionCloseReason reason) {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(5), [&] {
            return close_calls_ != 0U && close_reason_ == reason;
        });
    }

    std::vector<std::uint8_t> data() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_;
    }

    std::size_t close_calls() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return close_calls_;
    }

    int close_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return close_error_;
    }

private:
    std::size_t expected_size_{0};
    bool invalid_consume_{false};
    bool close_on_data_{false};
    mutable std::mutex mutex_{};
    std::condition_variable condition_{};
    std::vector<std::uint8_t> data_{};
    std::size_t data_calls_{0};
    std::size_t close_calls_{0};
    ConnectionCloseReason close_reason_{ConnectionCloseReason::local};
    int close_error_{0};
};

struct SocketPair {
    shmipc::transport::ControlSocket event_side{};
    shmipc::transport::ControlSocket peer{};
};

shmipc::transport::TransportResult<SocketPair> make_socket_pair() {
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return {{}, TransportError::system_error, errno};
    }
    auto event_side =
        shmipc::transport::adopt_control_socket(descriptors[0]);
    auto peer = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!event_side || !peer) {
        return {{}, TransportError::system_error,
                event_side ? peer.system_error : event_side.system_error};
    }
    return {{std::move(event_side.value), std::move(peer.value)},
            TransportError::none, 0};
}

bool test_buffered_read_write_and_remote_close() {
    auto dispatcher = shmipc::transport::start_epoll_dispatcher({8U, 1024U, 16});
    auto sockets = make_socket_pair();
    auto callback = std::make_shared<RecordingCallback>(6U);
    auto connection = dispatcher && sockets
                          ? dispatcher.value.add(
                                std::move(sockets.value.event_side), callback)
                          : shmipc::transport::EventConnectionResult{};
    const std::array<std::uint8_t, 3> first{{'a', 'b', 'c'}};
    const std::array<std::uint8_t, 3> second{{'d', 'e', 'f'}};
    if (!connection ||
        !sockets.value.peer.write_full(first.data(), first.size()) ||
        !callback->wait_for_data_calls(1U) ||
        !sockets.value.peer.write_full(second.data(), second.size()) ||
        !callback->wait_for_data()) {
        return false;
    }
    const std::array<std::uint8_t, 2> pong_first{{'p', 'o'}};
    const std::array<std::uint8_t, 2> pong_second{{'n', 'g'}};
    const std::vector<std::pair<const std::uint8_t*, std::size_t>> buffers{
        {pong_first.data(), pong_first.size()},
        {pong_second.data(), pong_second.size()}};
    std::array<std::uint8_t, 4> response{};
    const auto written = connection.value->writev(buffers);
    const auto read = written ? sockets.value.peer.read_full(
                                    response.data(), response.size())
                              : shmipc::transport::IoResult{};
    const std::array<std::uint8_t, 4> expected_response{{'p', 'o', 'n', 'g'}};
    const std::vector<std::uint8_t> expected_data{'a', 'b', 'c', 'd', 'e', 'f'};
    if (!read || response != expected_response ||
        callback->data() != expected_data) {
        return false;
    }
    static_cast<void>(sockets.value.peer.shutdown());
    static_cast<void>(sockets.value.peer.close());
    return callback->wait_for_close(ConnectionCloseReason::remote) &&
           callback->close_calls() == 1U;
}

bool test_write_backpressure_and_serialization() {
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    auto sockets = make_socket_pair();
    const int send_buffer = 4096;
    if (!dispatcher || !sockets ||
        ::setsockopt(sockets.value.event_side.native_handle(), SOL_SOCKET,
                     SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
        return false;
    }
    auto callback = std::make_shared<RecordingCallback>(1U << 20U);
    auto connection = dispatcher.value.add(
        std::move(sockets.value.event_side), callback);
    if (!connection) {
        return false;
    }
    constexpr std::size_t frame_size = 4096U;
    constexpr std::size_t frame_count = 64U;
    std::vector<std::uint8_t> frame_a(frame_size, 0xa5U);
    std::vector<std::uint8_t> frame_b(frame_size, 0x5aU);
    std::vector<std::uint8_t> received(frame_size * frame_count * 2U);
    bool reader_ok = false;
    std::thread reader([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto result = sockets.value.peer.read_full(received.data(),
                                                         received.size());
        reader_ok = static_cast<bool>(result);
    });
    bool writer_a = true;
    bool writer_b = true;
    std::thread first([&] {
        for (std::size_t index = 0; index < frame_count; ++index) {
            if (!connection.value->write(frame_a.data(), frame_a.size())) {
                writer_a = false;
                return;
            }
        }
    });
    std::thread second([&] {
        for (std::size_t index = 0; index < frame_count; ++index) {
            if (!connection.value->write(frame_b.data(), frame_b.size())) {
                writer_b = false;
                return;
            }
        }
    });
    first.join();
    second.join();
    reader.join();
    if (!reader_ok || !writer_a || !writer_b) {
        return false;
    }
    for (std::size_t frame = 0; frame < frame_count * 2U; ++frame) {
        const auto marker = received[frame * frame_size];
        if (marker != 0xa5U && marker != 0x5aU) {
            return false;
        }
        for (std::size_t offset = 1; offset < frame_size; ++offset) {
            if (received[frame * frame_size + offset] != marker) {
                return false;
            }
        }
    }
    return true;
}

bool test_close_reasons_and_limits() {
    auto local_dispatcher = shmipc::transport::start_epoll_dispatcher();
    auto local_sockets = make_socket_pair();
    auto local_callback = std::make_shared<RecordingCallback>(1U);
    auto local_connection = local_dispatcher && local_sockets
                                ? local_dispatcher.value.add(
                                      std::move(local_sockets.value.event_side),
                                      local_callback)
                                : shmipc::transport::EventConnectionResult{};
    if (!local_connection ||
        local_connection.value->close() != TransportError::none ||
        !local_callback->wait_for_close(ConnectionCloseReason::local) ||
        local_connection.value->close() != TransportError::none ||
        local_callback->close_calls() != 1U) {
        return false;
    }

    auto stop_dispatcher = shmipc::transport::start_epoll_dispatcher();
    auto stop_sockets = make_socket_pair();
    auto stop_callback = std::make_shared<RecordingCallback>(1U);
    auto stop_connection = stop_dispatcher && stop_sockets
                               ? stop_dispatcher.value.add(
                                     std::move(stop_sockets.value.event_side),
                                     stop_callback)
                               : shmipc::transport::EventConnectionResult{};
    if (!stop_connection ||
        stop_dispatcher.value.stop() != TransportError::none ||
        !stop_callback->wait_for_close(
            ConnectionCloseReason::dispatcher_shutdown)) {
        return false;
    }

    auto limit_dispatcher =
        shmipc::transport::start_epoll_dispatcher({8U, 16U, 16});
    auto limit_sockets = make_socket_pair();
    auto limit_callback = std::make_shared<RecordingCallback>(1024U);
    auto limit_connection = limit_dispatcher && limit_sockets
                                ? limit_dispatcher.value.add(
                                      std::move(limit_sockets.value.event_side),
                                      limit_callback)
                                : shmipc::transport::EventConnectionResult{};
    std::array<std::uint8_t, 17> too_much{};
    return limit_connection &&
           limit_sockets.value.peer.write_full(too_much.data(),
                                                too_much.size()) &&
           limit_callback->wait_for_close(ConnectionCloseReason::buffer_limit) &&
           limit_callback->close_error() == EMSGSIZE;
}

bool test_callback_contract_and_config() {
    auto invalid_config =
        shmipc::transport::start_epoll_dispatcher({0U, 16U, 1});
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    auto sockets = make_socket_pair();
    auto callback = std::make_shared<RecordingCallback>(1U, true);
    auto connection = dispatcher && sockets
                          ? dispatcher.value.add(
                                std::move(sockets.value.event_side), callback)
                          : shmipc::transport::EventConnectionResult{};
    const std::uint8_t byte = 1U;
    if (invalid_config ||
        invalid_config.error != TransportError::invalid_argument ||
        !connection || !sockets.value.peer.write_full(&byte, 1U) ||
        !callback->wait_for_close(ConnectionCloseReason::callback_error)) {
        return false;
    }

    auto close_dispatcher = shmipc::transport::start_epoll_dispatcher();
    auto close_sockets = make_socket_pair();
    auto close_callback =
        std::make_shared<RecordingCallback>(1U, false, true);
    auto close_connection = close_dispatcher && close_sockets
                                ? close_dispatcher.value.add(
                                      std::move(close_sockets.value.event_side),
                                      close_callback)
                                : shmipc::transport::EventConnectionResult{};
    return close_connection &&
           close_sockets.value.peer.write_full(&byte, 1U) &&
           close_callback->wait_for_close(ConnectionCloseReason::local) &&
           close_callback->close_calls() == 1U;
}

#else

bool test_unsupported() {
    const auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    return !dispatcher && dispatcher.error == TransportError::unsupported;
}

#endif

}  // namespace

int main() {
#ifdef __linux__
    if (!test_buffered_read_write_and_remote_close()) {
        std::cerr << "epoll buffered read/write/remote close test failed\n";
        return 1;
    }
    if (!test_write_backpressure_and_serialization()) {
        std::cerr << "epoll backpressure/serialization test failed\n";
        return 1;
    }
    if (!test_close_reasons_and_limits()) {
        std::cerr << "epoll close/limit test failed\n";
        return 1;
    }
    if (!test_callback_contract_and_config()) {
        std::cerr << "epoll callback/config test failed\n";
        return 1;
    }
#else
    if (!test_unsupported()) {
        std::cerr << "epoll unsupported-platform test failed\n";
        return 1;
    }
#endif
    return 0;
}
