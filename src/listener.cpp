#include "shmipc/listener.hpp"

#include "core/v2_multiplexed_session.hpp"
#include "public/session_impl.hpp"
#include "transport/control_socket.hpp"
#include "transport/epoll_dispatcher.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <limits>
#include <mutex>
#include <utility>

#include <poll.h>

namespace shmipc {
namespace {

using Clock = std::chrono::steady_clock;

Status map_listener_status(transport::TransportError error,
                           int system_error) noexcept {
    auto status = detail::map_transport_status(error, system_error);
    if (status.error == Error::transport_error) {
        status.error = Error::connection_error;
    }
    return status;
}

Status map_dispatcher_status(transport::TransportError error,
                             int system_error) noexcept {
    auto status = detail::map_transport_status(error, system_error);
    if (status.error == Error::transport_error) {
        status.error = Error::event_loop_error;
    }
    return status;
}

bool valid_config(const ListenerConfig& config) noexcept {
    return config.backlog > 0 &&
           config.max_handshake_frame_length >= 8U;
}

int poll_timeout(Clock::time_point deadline) noexcept {
    const auto now = Clock::now();
    if (deadline <= now) {
        return 0;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now);
    const auto bounded = std::min(remaining, std::chrono::milliseconds(50));
    return std::max(1, static_cast<int>(bounded.count()));
}

}  // namespace

struct Listener::Impl final {
    Impl(transport::ControlListener&& control_listener,
         std::shared_ptr<EventLoop> loop, SharedMemoryMode mode,
         std::uint32_t maximum_frame_length,
         std::uint16_t bound_port) noexcept
        : listener(std::move(control_listener)),
          event_loop(std::move(loop)),
          shared_memory_mode(mode),
          max_handshake_frame_length(maximum_frame_length),
          port(bound_port) {}

    transport::ControlListener listener{};
    std::shared_ptr<EventLoop> event_loop{};
    SharedMemoryMode shared_memory_mode{SharedMemoryMode::file};
    std::uint32_t max_handshake_frame_length{0U};
    std::uint16_t port{0U};
    std::atomic<bool> closed{false};
    std::mutex accept_mutex{};
    std::mutex listener_mutex{};
};

Listener::Listener(std::shared_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
Listener::Listener() noexcept = default;
Listener::~Listener() {
    static_cast<void>(close());
}
Listener::Listener(Listener&&) noexcept = default;
Listener& Listener::operator=(Listener&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Listener::operator bool() const noexcept {
    return impl_ != nullptr &&
           !impl_->closed.load(std::memory_order_acquire);
}

std::uint16_t Listener::local_port() const noexcept {
    return impl_ == nullptr ? 0U : impl_->port;
}

SessionResult Listener::accept_session(std::chrono::milliseconds timeout) {
    auto impl = impl_;
    if (impl == nullptr || impl->closed.load(std::memory_order_acquire)) {
        return {{}, detail::make_status(Error::closed, EBADF)};
    }
    if (timeout.count() < 0) {
        return {{}, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    const auto now = Clock::now();
    const auto maximum_timeout =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::time_point::max() - now);
    const auto deadline = timeout >= maximum_timeout
                              ? Clock::time_point::max()
                              : now + timeout;
    std::lock_guard<std::mutex> accept_lock(impl->accept_mutex);
    for (;;) {
        if (impl->closed.load(std::memory_order_acquire)) {
            return {{}, detail::make_status(Error::closed, EBADF)};
        }
        int descriptor = -1;
        {
            std::lock_guard<std::mutex> lock(impl->listener_mutex);
            descriptor = impl->listener.native_handle();
        }
        pollfd event{descriptor, POLLIN, 0};
        const auto waited = ::poll(&event, 1U, poll_timeout(deadline));
        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {{}, map_listener_status(
                            transport::TransportError::system_error, errno)};
        }
        if (impl->closed.load(std::memory_order_acquire)) {
            return {{}, detail::make_status(Error::closed, EBADF)};
        }
        if (waited == 0) {
            if (Clock::now() >= deadline) {
                return {{}, detail::make_status(Error::timeout, ETIMEDOUT)};
            }
            continue;
        }
        if ((event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return {{}, map_listener_status(
                            transport::TransportError::system_error, EIO)};
        }
        transport::ControlSocketResult accepted;
        {
            std::lock_guard<std::mutex> lock(impl->listener_mutex);
            if (impl->closed.load(std::memory_order_acquire)) {
                return {{}, detail::make_status(Error::closed, EBADF)};
            }
            accepted = impl->listener.accept();
        }
        if (!accepted) {
            if (accepted.error == transport::TransportError::would_block) {
                if (Clock::now() >= deadline) {
                    return {{}, detail::make_status(Error::timeout,
                                                    ETIMEDOUT)};
                }
                continue;
            }
            return {{}, map_listener_status(accepted.error,
                                            accepted.system_error)};
        }

        if (impl->shared_memory_mode == SharedMemoryMode::file) {
            auto result = core::start_v2_multiplexed_server_session(
                std::move(accepted.value), impl->event_loop->dispatcher,
                std::min(impl->max_handshake_frame_length,
                         core::v2_max_metadata_frame_length));
            if (!result) {
                return {{}, detail::map_session_status(result.status)};
            }
            return {Session(std::make_unique<Session::Impl>(
                        impl->event_loop, std::move(result.value))), {}};
        }
        auto result = core::start_v3_multiplexed_server_session(
            std::move(accepted.value), impl->event_loop->dispatcher,
            std::min(impl->max_handshake_frame_length,
                     core::v3_max_metadata_frame_length));
        if (!result) {
            const auto handshake =
                detail::map_v3_handshake_status(result.handshake_status);
            return {{}, handshake ? detail::map_session_status(result.status)
                                  : handshake};
        }
        return {Session(std::make_unique<Session::Impl>(
                    impl->event_loop, std::move(result.value))), {}};
    }
}

Status Listener::close() noexcept {
    auto impl = impl_;
    if (impl == nullptr ||
        impl->closed.exchange(true, std::memory_order_acq_rel)) {
        return {};
    }
    std::lock_guard<std::mutex> lock(impl->listener_mutex);
    return detail::map_transport_status(impl->listener.close(), 0);
}

ListenerResult listen_tcp(const std::string& host, std::uint16_t port,
                          const ListenerConfig& config) {
    if (!valid_config(config)) {
        return {{}, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    if (config.shared_memory_mode == SharedMemoryMode::memfd) {
        return {{}, detail::make_status(Error::unsupported, ENOTSUP)};
    }
    auto dispatcher = transport::start_epoll_dispatcher();
    if (!dispatcher) {
        return {{}, map_dispatcher_status(dispatcher.error,
                                          dispatcher.system_error)};
    }
    auto listener = transport::listen_tcp(host, port, config.backlog);
    if (!listener) {
        return {{}, map_listener_status(listener.error,
                                        listener.system_error)};
    }
    const auto bound_port = listener.value.local_port();
    if (!bound_port) {
        return {{}, map_listener_status(bound_port.error,
                                        bound_port.system_error)};
    }
    const auto nonblocking = listener.value.set_nonblocking(true);
    if (nonblocking != transport::TransportError::none) {
        return {{}, map_listener_status(nonblocking, errno)};
    }
    auto event_loop =
        std::make_shared<EventLoop>(std::move(dispatcher.value));
    return {Listener(std::make_shared<Listener::Impl>(
                std::move(listener.value), std::move(event_loop),
                config.shared_memory_mode, config.max_handshake_frame_length,
                bound_port.value)), {}};
}

ListenerResult listen_unix(const std::string& path,
                           const ListenerConfig& config) {
    if (!valid_config(config)) {
        return {{}, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    auto dispatcher = transport::start_epoll_dispatcher();
    if (!dispatcher) {
        return {{}, map_dispatcher_status(dispatcher.error,
                                          dispatcher.system_error)};
    }
    auto listener = transport::listen_unix(path, config.backlog);
    if (!listener) {
        return {{}, map_listener_status(listener.error,
                                        listener.system_error)};
    }
    const auto nonblocking = listener.value.set_nonblocking(true);
    if (nonblocking != transport::TransportError::none) {
        return {{}, map_listener_status(nonblocking, errno)};
    }
    auto event_loop =
        std::make_shared<EventLoop>(std::move(dispatcher.value));
    return {Listener(std::make_shared<Listener::Impl>(
                std::move(listener.value), std::move(event_loop),
                config.shared_memory_mode, config.max_handshake_frame_length,
                0U)), {}};
}

}  // namespace shmipc
