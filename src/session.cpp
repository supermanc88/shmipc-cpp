#include "shmipc/session.hpp"

#include "core/v2_multiplexed_session.hpp"
#include "public/session_impl.hpp"
#include "transport/control_socket.hpp"
#include "transport/epoll_dispatcher.hpp"

#include <atomic>
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

void emit_log(const std::shared_ptr<Logger>& logger, LogLevel threshold,
              LogLevel level, const std::string& component,
              const std::string& message) noexcept {
    if (logger == nullptr || threshold == LogLevel::off ||
        static_cast<int>(level) < static_cast<int>(threshold)) {
        return;
    }
    try {
        logger->log(level, component, message);
    } catch (...) {
    }
}

}  // namespace detail

namespace {

using detail::make_status;
using detail::map_session_status;
using detail::map_transport_status;
using detail::map_v3_handshake_status;

std::atomic<std::uint64_t> next_session_id{0U};

void emit_session_log(const std::shared_ptr<Logger>& logger,
                      LogLevel threshold, LogLevel level,
                      std::uint64_t session_id,
                      const char* message) noexcept {
    try {
        detail::emit_log(logger, threshold, level,
                         "session." + std::to_string(session_id), message);
    } catch (...) {
    }
}

void emit_stream_log(const std::shared_ptr<Logger>& logger,
                     LogLevel threshold, LogLevel level,
                     std::uint64_t session_id, std::uint32_t stream_id,
                     const char* message) noexcept {
    try {
        detail::emit_log(logger, threshold, level,
                         "session." + std::to_string(session_id) +
                             ".stream." + std::to_string(stream_id),
                         message);
    } catch (...) {
    }
}

bool valid_config(const ClientConfig& config) {
    if (config.queue_name.empty() || config.buffer_name.empty() ||
        config.queue_name == config.buffer_name ||
        config.queue_capacity == 0U || config.buffer_size < (1U << 20U) ||
        config.buffer_size > std::numeric_limits<std::uint32_t>::max() ||
        config.buffer_tiers.empty() ||
        config.metrics_interval.count() <= 0 ||
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

const char* to_string(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::trace:
            return "trace";
        case LogLevel::debug:
            return "debug";
        case LogLevel::info:
            return "info";
        case LogLevel::warning:
            return "warning";
        case LogLevel::error:
            return "error";
        case LogLevel::off:
            return "off";
    }
    return "unknown";
}

Session::Impl::Impl(std::shared_ptr<EventLoop> loop,
                    core::V2MultiplexedClientSession&& value,
                    std::shared_ptr<Monitor> session_monitor,
                    std::chrono::milliseconds interval,
                    std::shared_ptr<Logger> session_logger,
                    LogLevel session_log_level)
    : event_loop(std::move(loop)),
      session(std::move(value)),
      session_id(next_session_id.fetch_add(1U, std::memory_order_relaxed) + 1U),
      monitor(std::move(session_monitor)),
      metrics_interval(interval),
      logger(std::move(session_logger)),
      log_level(session_log_level) {
    start_telemetry();
    emit_session_log(logger, log_level, LogLevel::info, session_id,
                     "started client session");
}

Session::Impl::Impl(std::shared_ptr<EventLoop> loop,
                    core::V2MultiplexedServerSession&& value,
                    std::shared_ptr<Monitor> session_monitor,
                    std::chrono::milliseconds interval,
                    std::shared_ptr<Logger> session_logger,
                    LogLevel session_log_level)
    : event_loop(std::move(loop)),
      session(std::move(value)),
      session_id(next_session_id.fetch_add(1U, std::memory_order_relaxed) + 1U),
      monitor(std::move(session_monitor)),
      metrics_interval(interval),
      logger(std::move(session_logger)),
      log_level(session_log_level) {
    start_telemetry();
    emit_session_log(logger, log_level, LogLevel::info, session_id,
                     "started server session");
}

Session::Impl::~Impl() {
    stop_telemetry();
}

SessionMetrics Session::Impl::metrics() const noexcept {
    const auto internal = std::visit(
        [](const auto& value) { return value.metrics(); }, session);
    SessionMetrics result;
    result.session_id = session_id;
    result.is_client = is_client();
    result.protocol_version = internal.protocol_version;
    result.performance.received_polling_events =
        internal.received_polling_events;
    result.performance.sent_polling_events = internal.sent_polling_events;
    result.performance.bytes_sent = internal.bytes_sent;
    result.performance.bytes_received = internal.bytes_received;
    result.performance.send_queue_depth = internal.send_queue_depth;
    result.performance.receive_queue_depth = internal.receive_queue_depth;
    result.stability.shared_memory_allocation_errors =
        internal.shared_memory_allocation_errors;
    result.stability.fallback_writes = internal.fallback_writes;
    result.stability.fallback_reads = internal.fallback_reads;
    result.stability.control_connection_errors =
        internal.control_connection_errors;
    result.stability.queue_full_errors = internal.queue_full_errors;
    result.stability.active_streams = internal.active_streams;
    result.shared_memory.capacity_bytes =
        internal.shared_memory_capacity_bytes;
    result.shared_memory.used_bytes = internal.shared_memory_used_bytes;
    return result;
}

void Session::Impl::start_telemetry() {
    if (monitor != nullptr) {
        telemetry_worker = std::thread([this] { telemetry_loop(); });
    }
}

void Session::Impl::emit_metrics() noexcept {
    try {
        monitor->on_session_metrics(metrics());
    } catch (...) {
        emit_session_log(logger, log_level, LogLevel::error, session_id,
                         "monitor metrics callback threw an exception");
    }
}

void Session::Impl::telemetry_loop() noexcept {
    std::unique_lock<std::mutex> lock(telemetry_mutex);
    while (!telemetry_stopping) {
        if (telemetry_condition.wait_for(lock, metrics_interval, [this] {
                return telemetry_stopping;
            })) {
            break;
        }
        lock.unlock();
        emit_metrics();
        lock.lock();
    }
    lock.unlock();
    emit_metrics();
    Status flushed;
    try {
        flushed = monitor->flush();
    } catch (...) {
        flushed = make_status(Error::callback_error);
    }
    if (!flushed) {
        emit_session_log(logger, log_level, LogLevel::error, session_id,
                         "monitor flush failed");
    }
}

void Session::Impl::stop_telemetry() noexcept {
    if (monitor == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex);
        telemetry_stopping = true;
    }
    telemetry_condition.notify_all();
    if (telemetry_worker.joinable()) {
        telemetry_worker.join();
    }
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

bool Stream::is_open() const noexcept {
    return impl_ != nullptr && !impl_->closed.load(std::memory_order_acquire) &&
           impl_->stream.is_open();
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
    const auto was_fallback = impl_->stream.is_fallback();
    const auto status = map_session_status(impl_->stream.send(data, size));
    if (status && !was_fallback && impl_->stream.is_fallback()) {
        emit_stream_log(impl_->logger, impl_->log_level, LogLevel::warning,
                        impl_->session_id, impl_->stream.id(),
                        "entered fallback mode while sending");
    }
    return status;
}

Status Stream::send(const std::vector<std::uint8_t>& data) {
    return send(data.data(), data.size());
}

MessageResult Stream::receive(std::chrono::milliseconds timeout) {
    if (impl_ == nullptr) {
        return {{}, make_status(Error::closed, EBADF)};
    }
    const auto was_fallback = impl_->stream.is_fallback();
    auto result = impl_->stream.receive(timeout);
    if (result && !was_fallback && impl_->stream.is_fallback()) {
        emit_stream_log(impl_->logger, impl_->log_level, LogLevel::warning,
                        impl_->session_id, impl_->stream.id(),
                        "entered fallback mode while receiving");
    }
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

SessionMetrics Session::metrics() const noexcept {
    return impl_ == nullptr ? SessionMetrics{} : impl_->metrics();
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
    return {Stream(std::make_shared<Stream::Impl>(
                std::move(result.value), impl_->logger, impl_->log_level,
                impl_->session_id)), {}};
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
    return {Stream(std::make_shared<Stream::Impl>(
                std::move(result.value), impl_->logger, impl_->log_level,
                impl_->session_id)), {}};
}

Status Session::close() noexcept {
    if (impl_ == nullptr) {
        return {};
    }
    auto impl = std::move(impl_);
    emit_session_log(impl->logger, impl->log_level, LogLevel::info,
                     impl->session_id, "closing session");
    impl->stop_telemetry();
    const auto session_status = std::visit([](auto& session) {
        return map_session_status(session.close());
    }, impl->session);
    emit_session_log(impl->logger, impl->log_level, LogLevel::info,
                     impl->session_id, "closed session");
    return session_status;
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
                    std::move(event_loop), std::move(result.value),
                    config.monitor, config.metrics_interval, config.logger,
                    config.log_level)), {}};
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
                std::move(event_loop), std::move(result.value), config.monitor,
                config.metrics_interval, config.logger, config.log_level)), {}};
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
