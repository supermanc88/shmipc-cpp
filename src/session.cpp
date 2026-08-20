#include "shmipc/session.hpp"

#include "core/v2_multiplexed_session.hpp"
#include "public/session_impl.hpp"
#include "transport/control_socket.hpp"
#include "transport/epoll_dispatcher.hpp"

#include <cerrno>
#include <limits>
#include <unordered_set>
#include <utility>
#include <variant>

namespace shmipc {
namespace detail {

Status make_status(Error error, int system_error) noexcept {
    return {error, system_error};
}

Status map_transport_status(transport::TransportError error,
                            int system_error) noexcept {
    switch (error) {
        case transport::TransportError::none:
            return {};
        case transport::TransportError::invalid_argument:
            return make_status(Error::invalid_argument, system_error);
        case transport::TransportError::unsupported:
            return make_status(Error::unsupported, system_error);
        case transport::TransportError::end_of_stream:
        case transport::TransportError::invalid_state:
            return make_status(Error::closed, system_error);
        case transport::TransportError::would_block:
            return make_status(Error::timeout, system_error);
        case transport::TransportError::buffer_limit:
        case transport::TransportError::callback_error:
        case transport::TransportError::system_error:
            return make_status(Error::transport_error, system_error);
    }
    return make_status(Error::transport_error, system_error);
}

Status map_session_status(const core::V2SessionStatus& status) noexcept {
    using InternalError = core::V2SessionError;
    switch (status.error) {
        case InternalError::none:
            return {};
        case InternalError::invalid_argument:
            return make_status(Error::invalid_argument, status.system_error);
        case InternalError::handshake_error:
            return make_status(Error::handshake_error, status.system_error);
        case InternalError::dispatcher_error:
            return make_status(Error::event_loop_error, status.system_error);
        case InternalError::transport_error:
            return make_status(Error::transport_error, status.system_error);
        case InternalError::codec_error:
        case InternalError::unexpected_event:
        case InternalError::unexpected_stream:
            return make_status(Error::protocol_error, status.system_error);
        case InternalError::queue_error:
        case InternalError::buffer_pool_error:
        case InternalError::buffer_io_error:
            return make_status(Error::shared_memory_error, status.system_error);
        case InternalError::unhealthy:
            return make_status(Error::unhealthy, status.system_error);
        case InternalError::closed:
            return make_status(Error::closed, status.system_error);
        case InternalError::timeout:
            return make_status(Error::timeout, status.system_error);
    }
    return make_status(Error::protocol_error, status.system_error);
}

Status map_v3_handshake_status(const core::V3HandshakeStatus& status) noexcept {
    switch (status.error) {
        case core::V3HandshakeError::none:
            return {};
        case core::V3HandshakeError::invalid_argument:
            return make_status(Error::invalid_argument, status.system_error);
        case core::V3HandshakeError::unsupported:
            return make_status(Error::unsupported, status.system_error);
        default:
            return make_status(Error::handshake_error, status.system_error);
    }
}

}  // namespace detail

namespace {

using detail::make_status;
using detail::map_session_status;
using detail::map_transport_status;
using detail::map_v3_handshake_status;

bool valid_config(const ClientConfig& config) {
    if (config.queue_name.empty() || config.buffer_name.empty() ||
        config.queue_name == config.buffer_name ||
        config.queue_capacity == 0U || config.buffer_size < (1U << 20U) ||
        config.buffer_size > std::numeric_limits<std::uint32_t>::max() ||
        config.buffer_tiers.empty() ||
        config.buffer_tiers.size() >
            std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    std::uint32_t total_percent = 0U;
    std::unordered_set<std::uint32_t> capacities;
    capacities.reserve(config.buffer_tiers.size());
    for (const auto& tier : config.buffer_tiers) {
        if (tier.capacity == 0U || tier.capacity % 4U != 0U ||
            tier.percent == 0U || tier.percent > 100U ||
            total_percent > 100U - tier.percent ||
            !capacities.insert(tier.capacity).second) {
            return false;
        }
        total_percent += tier.percent;
    }
    return total_percent == 100U;
}

std::vector<shm::BufferTierSpec> map_tiers(const ClientConfig& config) {
    std::vector<shm::BufferTierSpec> tiers;
    tiers.reserve(config.buffer_tiers.size());
    for (const auto& tier : config.buffer_tiers) {
        tiers.push_back({tier.capacity, tier.percent});
    }
    return tiers;
}

}  // namespace

const char* to_string(Error error) noexcept {
    switch (error) {
        case Error::none:
            return "none";
        case Error::invalid_argument:
            return "invalid argument";
        case Error::unsupported:
            return "unsupported";
        case Error::connection_error:
            return "connection error";
        case Error::handshake_error:
            return "handshake error";
        case Error::event_loop_error:
            return "event loop error";
        case Error::transport_error:
            return "transport error";
        case Error::protocol_error:
            return "protocol error";
        case Error::shared_memory_error:
            return "shared memory error";
        case Error::unhealthy:
            return "unhealthy";
        case Error::closed:
            return "closed";
        case Error::timeout:
            return "timeout";
        case Error::callback_already_set:
            return "callback already set";
        case Error::callback_error:
            return "callback error";
    }
    return "unknown error";
}

Stream::Stream(std::shared_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Stream::Stream() noexcept = default;
Stream::~Stream() = default;
Stream::Stream(Stream&&) noexcept = default;
Stream& Stream::operator=(Stream&&) noexcept = default;

Stream::operator bool() const noexcept {
    return impl_ != nullptr &&
           !impl_->closed.load(std::memory_order_acquire) &&
           static_cast<bool>(impl_->stream);
}

std::uint32_t Stream::id() const noexcept {
    return impl_ == nullptr ? 0U : impl_->stream.id();
}

bool Stream::is_fallback() const noexcept {
    return impl_ != nullptr && impl_->stream.is_fallback();
}

Status Stream::send(const std::uint8_t* data, std::size_t size) {
    if (impl_ == nullptr) {
        return make_status(Error::closed, EBADF);
    }
    return map_session_status(impl_->stream.send(data, size));
}

Status Stream::send(const std::vector<std::uint8_t>& data) {
    return send(data.data(), data.size());
}

MessageResult Stream::receive(std::chrono::milliseconds timeout) {
    if (impl_ == nullptr) {
        return {{}, make_status(Error::closed, EBADF)};
    }
    auto result = impl_->stream.receive(timeout);
    return {std::move(result.value), map_session_status(result.status)};
}

void Stream::set_deadline(std::optional<Deadline> deadline) noexcept {
    if (impl_ != nullptr) {
        impl_->stream.set_deadline(deadline);
    }
}

void Stream::set_read_deadline(std::optional<Deadline> deadline) noexcept {
    if (impl_ != nullptr) {
        impl_->stream.set_read_deadline(deadline);
    }
}

void Stream::set_write_deadline(std::optional<Deadline> deadline) noexcept {
    if (impl_ != nullptr) {
        impl_->stream.set_write_deadline(deadline);
    }
}

Status Stream::close() {
    if (impl_ == nullptr) {
        return {};
    }
    auto impl = impl_;
    impl->local_close_requested.store(true, std::memory_order_release);
    impl->closed.store(true, std::memory_order_release);
    std::shared_ptr<AsyncCallbackControl> callback_control;
    {
        std::lock_guard<std::mutex> lock(impl->callback_mutex);
        callback_control = impl->callback_control.lock();
    }
    const auto status = map_session_status(impl->stream.close());
    if (callback_control && !is_callback_executor_thread()) {
        callback_control->wait_until_finished();
    }
    impl_.reset();
    return status;
}

Status Stream::wait_remote_close(std::chrono::milliseconds timeout) {
    if (impl_ == nullptr) {
        return make_status(Error::closed, EBADF);
    }
    return map_session_status(impl_->stream.wait_remote_close(timeout));
}

Session::Session(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Session::Session() noexcept = default;

Session::~Session() {
    static_cast<void>(close());
}

Session::Session(Session&&) noexcept = default;
Session& Session::operator=(Session&&) noexcept = default;

Session::operator bool() const noexcept {
    if (impl_ == nullptr) {
        return false;
    }
    return std::visit([](const auto& session) {
        return static_cast<bool>(session);
    }, impl_->session);
}

bool Session::is_open() const noexcept {
    return impl_ != nullptr && std::visit([](const auto& session) {
        return session.is_open();
    }, impl_->session);
}

bool Session::is_healthy() const noexcept {
    return impl_ != nullptr && std::visit([](const auto& session) {
        return session.is_healthy();
    }, impl_->session);
}

StreamResult Session::open_stream() {
    if (impl_ == nullptr) {
        return {{}, make_status(Error::closed, EBADF)};
    }
    if (!impl_->is_client()) {
        return {{}, make_status(Error::unsupported)};
    }
    auto& session =
        std::get<core::V2MultiplexedClientSession>(impl_->session);
    auto result = session.open_stream();
    if (!result) {
        return {{}, map_session_status(result.status)};
    }
    return {Stream(std::make_shared<Stream::Impl>(std::move(result.value))), {}};
}

StreamResult Session::accept_stream(std::chrono::milliseconds timeout) {
    if (impl_ == nullptr) {
        return {{}, make_status(Error::closed, EBADF)};
    }
    if (impl_->is_client()) {
        return {{}, make_status(Error::unsupported)};
    }
    auto& session =
        std::get<core::V2MultiplexedServerSession>(impl_->session);
    auto result = session.accept_stream(timeout);
    if (!result) {
        return {{}, map_session_status(result.status)};
    }
    return {Stream(std::make_shared<Stream::Impl>(std::move(result.value))), {}};
}

Status Session::close() noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    auto status = std::visit([](auto& session) {
        return map_session_status(session.close());
    }, impl_->session);
    impl_.reset();
    return status;
}

SessionResult Session::start(int socket_descriptor,
                             const ClientConfig& config) {
    auto socket = transport::adopt_control_socket(socket_descriptor);
    if (!socket) {
        return {{}, map_transport_status(socket.error, socket.system_error)};
    }
    auto dispatcher = transport::start_epoll_dispatcher();
    if (!dispatcher) {
        auto status = map_transport_status(dispatcher.error,
                                           dispatcher.system_error);
        if (status.error == Error::transport_error) {
            status.error = Error::event_loop_error;
        }
        return {{}, status};
    }
    if (config.shared_memory_mode == SharedMemoryMode::file) {
        const core::V2ClientConfig internal_config{
            config.queue_name,
            config.buffer_name,
            config.queue_capacity,
            config.buffer_size,
            map_tiers(config),
        };
        auto result = core::start_v2_multiplexed_client_session(
            std::move(socket.value), internal_config, dispatcher.value);
        if (!result) {
            return {{}, map_session_status(result.status)};
        }
        auto event_loop = std::make_shared<EventLoop>(
            std::move(dispatcher.value));
        return {Session(std::make_unique<Session::Impl>(
                    std::move(event_loop), std::move(result.value))), {}};
    }

    const core::V3ClientConfig internal_config{
        config.queue_name,
        config.buffer_name,
        config.queue_capacity,
        config.buffer_size,
        map_tiers(config),
    };
    auto result = core::start_v3_multiplexed_client_session(
        std::move(socket.value), internal_config, dispatcher.value);
    if (!result) {
        const auto handshake_status =
            map_v3_handshake_status(result.handshake_status);
        return {{}, handshake_status ? map_session_status(result.status)
                                     : handshake_status};
    }
    auto event_loop = std::make_shared<EventLoop>(std::move(dispatcher.value));
    return {Session(std::make_unique<Session::Impl>(
                std::move(event_loop), std::move(result.value))), {}};
}

SessionResult connect_tcp(const std::string& host, std::uint16_t port,
                          const ClientConfig& config) {
    if (config.shared_memory_mode == SharedMemoryMode::memfd) {
        return {{}, make_status(Error::unsupported, ENOTSUP)};
    }
    if (!valid_config(config)) {
        return {{}, make_status(Error::invalid_argument, EINVAL)};
    }
    auto socket = transport::connect_tcp(host, port);
    if (!socket) {
        auto status = map_transport_status(socket.error, socket.system_error);
        if (status.error == Error::transport_error) {
            status.error = Error::connection_error;
        }
        return {{}, status};
    }
    return Session::start(socket.value.release(), config);
}

SessionResult connect_unix(const std::string& path,
                           const ClientConfig& config) {
    if (!valid_config(config)) {
        return {{}, make_status(Error::invalid_argument, EINVAL)};
    }
    auto socket = transport::connect_unix(path);
    if (!socket) {
        auto status = map_transport_status(socket.error, socket.system_error);
        if (status.error == Error::transport_error) {
            status.error = Error::connection_error;
        }
        return {{}, status};
    }
    return Session::start(socket.value.release(), config);
}

}  // namespace shmipc
