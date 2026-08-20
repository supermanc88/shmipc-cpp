#pragma once

#include "shmipc/session.hpp"

#include "core/v2_multiplexed_session.hpp"

#include <atomic>
#include <memory>
#include <mutex>
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

}  // namespace detail

struct AsyncCallbackControl {
    virtual ~AsyncCallbackControl() = default;
    virtual void wait_until_finished() noexcept = 0;
};

[[nodiscard]] bool is_callback_executor_thread() noexcept;

struct Stream::Impl final {
    explicit Impl(core::V2Stream&& value) noexcept : stream(std::move(value)) {}

    core::V2Stream stream{};
    std::mutex callback_mutex{};
    std::weak_ptr<AsyncCallbackControl> callback_control{};
    std::atomic<bool> local_close_requested{false};
    std::atomic<bool> closed{false};
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
         core::V2MultiplexedClientSession&& value) noexcept
        : event_loop(std::move(loop)), session(std::move(value)) {}

    Impl(std::shared_ptr<EventLoop> loop,
         core::V2MultiplexedServerSession&& value) noexcept
        : event_loop(std::move(loop)), session(std::move(value)) {}

    [[nodiscard]] bool is_client() const noexcept {
        return std::holds_alternative<core::V2MultiplexedClientSession>(
            session);
    }

    std::shared_ptr<EventLoop> event_loop{};
    CoreSession session;
};

}  // namespace shmipc
