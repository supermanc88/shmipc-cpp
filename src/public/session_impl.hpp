#pragma once

#include "shmipc/session.hpp"

#include "core/v2_multiplexed_session.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>

namespace shmipc {

namespace detail {

[[nodiscard]] Status make_status(Error error,
                                 int system_error = 0) noexcept;
[[nodiscard]] Status map_transport_status(transport::TransportError error,
                                          int system_error) noexcept;
[[nodiscard]] Status map_session_status(
    const core::V2SessionStatus& status) noexcept;
[[nodiscard]] Status map_v3_handshake_status(
    const core::V3HandshakeStatus& status) noexcept;
void emit_log(const std::shared_ptr<Logger>& logger, LogLevel threshold,
              LogLevel level, const std::string& component,
              const std::string& message) noexcept;

}  // namespace detail

struct AsyncCallbackControl {
    virtual ~AsyncCallbackControl() = default;
    virtual void wait_until_finished() noexcept = 0;
};

[[nodiscard]] bool is_callback_executor_thread() noexcept;

struct Stream::Impl final {
    Impl(core::V2Stream&& value, std::shared_ptr<Logger> stream_logger,
         LogLevel stream_log_level, std::uint64_t owner_session_id) noexcept
        : stream(std::move(value)),
          logger(std::move(stream_logger)),
          log_level(stream_log_level),
          session_id(owner_session_id) {}

    core::V2Stream stream{};
    std::mutex callback_mutex{};
    std::weak_ptr<AsyncCallbackControl> callback_control{};
    std::atomic<bool> local_close_requested{false};
    std::atomic<bool> closed{false};
    std::shared_ptr<Logger> logger{};
    LogLevel log_level{LogLevel::warning};
    std::uint64_t session_id{0U};
};

struct EventLoop final {
    explicit EventLoop(transport::EpollDispatcher&& value) noexcept
        : dispatcher(std::move(value)) {}

    ~EventLoop() {
        static_cast<void>(dispatcher.stop());
    }

    transport::EpollDispatcher dispatcher{};
};

struct Session::Impl final {
    using CoreSession =
        std::variant<core::V2MultiplexedClientSession,
                     core::V2MultiplexedServerSession>;

    Impl(std::shared_ptr<EventLoop> loop,
         core::V2MultiplexedClientSession&& value,
         std::shared_ptr<Monitor> session_monitor,
         std::chrono::milliseconds interval,
         std::shared_ptr<Logger> session_logger,
         LogLevel session_log_level);

    Impl(std::shared_ptr<EventLoop> loop,
         core::V2MultiplexedServerSession&& value,
         std::shared_ptr<Monitor> session_monitor,
         std::chrono::milliseconds interval,
         std::shared_ptr<Logger> session_logger,
         LogLevel session_log_level);

    ~Impl();

    [[nodiscard]] bool is_client() const noexcept {
        return std::holds_alternative<core::V2MultiplexedClientSession>(
            session);
    }

    [[nodiscard]] SessionMetrics metrics() const noexcept;
    void stop_telemetry() noexcept;

private:
    void start_telemetry();
    void telemetry_loop() noexcept;
    void emit_metrics() noexcept;

public:
    std::shared_ptr<EventLoop> event_loop{};
    CoreSession session;
    const std::uint64_t session_id;
    std::shared_ptr<Monitor> monitor{};
    std::chrono::milliseconds metrics_interval{30000};
    std::shared_ptr<Logger> logger{};
    LogLevel log_level{LogLevel::warning};
    std::mutex telemetry_mutex{};
    std::condition_variable telemetry_condition{};
    std::thread telemetry_worker{};
    bool telemetry_stopping{false};
};

}  // namespace shmipc
