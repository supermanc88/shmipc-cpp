#pragma once

#include "transport/control_socket.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace shmipc::transport {

enum class ConnectionCloseReason {
    local,
    remote,
    io_error,
    buffer_limit,
    callback_error,
    dispatcher_shutdown,
};

struct ConsumeResult {
    std::size_t consumed{0};
    TransportError error{TransportError::none};
    int system_error{0};
};

class EventConnection;

class ControlEventCallback {
public:
    virtual ~ControlEventCallback() = default;

    [[nodiscard]] virtual ConsumeResult on_data(
        const std::uint8_t* data, std::size_t size,
        EventConnection& connection) = 0;
    virtual void on_close(ConnectionCloseReason reason,
                          int system_error) = 0;
};

struct EpollDispatcherConfig {
    std::size_t read_chunk_size{64U * 1024U};
    std::size_t max_buffered_bytes{64U * 1024U * 1024U + 8U};
    int max_events{128};
};

struct EpollState;

class EventConnection final {
public:
    ~EventConnection();

    EventConnection(const EventConnection&) = delete;
    EventConnection& operator=(const EventConnection&) = delete;
    EventConnection(EventConnection&&) = delete;
    EventConnection& operator=(EventConnection&&) = delete;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] int native_handle() const noexcept;
    [[nodiscard]] IoResult write(const std::uint8_t* data,
                                 std::size_t size) noexcept;
    [[nodiscard]] IoResult writev(
        const std::vector<std::pair<const std::uint8_t*, std::size_t>>& buffers)
        noexcept;
    [[nodiscard]] TransportError close() noexcept;

private:
    friend class EpollDispatcher;
    friend struct EpollState;

    EventConnection(ControlSocket&& socket,
                    std::shared_ptr<ControlEventCallback> callback,
                    std::weak_ptr<EpollState> dispatcher,
                    std::size_t read_chunk_size,
                    std::size_t max_buffered_bytes);

    [[nodiscard]] IoResult write_locked(const std::uint8_t* data,
                                        std::size_t size) noexcept;
    void handle_events(std::uint32_t events) noexcept;
    void handle_read() noexcept;
    void notify_writable() noexcept;
    void close_internal(ConnectionCloseReason reason, int system_error,
                        bool unregister) noexcept;

    ControlSocket socket_{};
    const int native_fd_{-1};
    std::shared_ptr<ControlEventCallback> callback_{};
    std::weak_ptr<EpollState> dispatcher_{};
    std::vector<std::uint8_t> read_buffer_{};
    std::size_t read_offset_{0};
    std::size_t read_chunk_size_{0};
    std::size_t max_buffered_bytes_{0};
    std::atomic<bool> closed_{false};
    std::mutex fd_mutex_{};
    std::mutex write_mutex_{};
    std::mutex writable_mutex_{};
    std::condition_variable writable_cv_{};
    std::uint64_t writable_generation_{0};
    std::recursive_mutex callback_mutex_{};
    bool pending_close_callback_{false};
    ConnectionCloseReason pending_close_reason_{ConnectionCloseReason::local};
    int pending_close_error_{0};
};

using EventConnectionResult =
    TransportResult<std::shared_ptr<EventConnection>>;

class EpollDispatcher final {
public:
    EpollDispatcher() noexcept = default;
    ~EpollDispatcher();

    EpollDispatcher(const EpollDispatcher&) = delete;
    EpollDispatcher& operator=(const EpollDispatcher&) = delete;
    EpollDispatcher(EpollDispatcher&& other) noexcept;
    EpollDispatcher& operator=(EpollDispatcher&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] EventConnectionResult add(
        ControlSocket&& socket,
        std::shared_ptr<ControlEventCallback> callback) noexcept;
    [[nodiscard]] TransportError stop() noexcept;

private:
    friend TransportResult<EpollDispatcher> start_epoll_dispatcher(
        const EpollDispatcherConfig&);
    explicit EpollDispatcher(std::shared_ptr<EpollState> state) noexcept;

    std::shared_ptr<EpollState> state_{};
};

using EpollDispatcherResult = TransportResult<EpollDispatcher>;

[[nodiscard]] const char* to_string(ConnectionCloseReason reason) noexcept;
[[nodiscard]] EpollDispatcherResult start_epoll_dispatcher(
    const EpollDispatcherConfig& config = {});

}  // namespace shmipc::transport
