#include "core/v2_client_session.hpp"
#include "core/v2_server_session.hpp"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace shmipc::core {

namespace {

constexpr std::uint32_t stream_opened = 0U;
constexpr std::uint32_t stream_closed = 1U;
constexpr std::size_t stream_close_frame_size = protocol::header_size + 4U;

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

V2SessionStatus session_error(V2SessionError error) {
    V2SessionStatus status{};
    status.error = error;
    return status;
}

V2SessionStatus transport_error(const transport::IoResult& result) {
    auto status = session_error(V2SessionError::transport_error);
    status.transport_error = result.error;
    status.system_error = result.system_error;
    return status;
}

V2SessionStatus queue_error(shm::QueueError error) {
    auto status = session_error(V2SessionError::queue_error);
    status.queue_error = error;
    return status;
}

}  // namespace

struct V2SingleStreamSessionState final : transport::ControlEventCallback {
    V2SingleStreamSessionState(V2SharedMemory&& shared_memory,
                               std::uint32_t stream_id) noexcept
        : shared_memory(std::move(shared_memory)),
          stream_id(stream_id) {}

    bool bind_or_validate_stream(std::uint32_t candidate) {
        if (candidate == 0U) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        if (stream_id == 0U) {
            stream_id = candidate;
            condition.notify_all();
            return true;
        }
        return stream_id == candidate;
    }

    std::uint32_t current_stream_id() const noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        return stream_id;
    }

    transport::ConsumeResult on_data(
        const std::uint8_t* data, std::size_t size,
        transport::EventConnection&) override {
        std::size_t consumed = 0;
        while (size - consumed >= protocol::header_size) {
            const auto header = protocol::decode_header(
                data + consumed, size - consumed);
            if (!header) {
                auto status = session_error(V2SessionError::codec_error);
                status.codec_error = header.error;
                fail(status);
                return {consumed, transport::TransportError::callback_error,
                        0};
            }
            if (header.value.length > size - consumed) {
                break;
            }
            if (header.value.version != v2_protocol_version) {
                fail(session_error(V2SessionError::unexpected_event));
                return {consumed, transport::TransportError::callback_error,
                        0};
            }
            if (header.value.type == protocol::EventType::polling) {
                if (header.value.length != protocol::header_size) {
                    fail(session_error(V2SessionError::unexpected_event));
                    return {consumed,
                            transport::TransportError::callback_error, 0};
                }
                const auto status = drain_receive_queue();
                if (!status) {
                    fail(status);
                    return {consumed,
                            transport::TransportError::callback_error, 0};
                }
            } else if (header.value.type == protocol::EventType::stream_close) {
                if (header.value.length != stream_close_frame_size ||
                    !bind_or_validate_stream(
                        read_u32(data + consumed + protocol::header_size))) {
                    fail(session_error(V2SessionError::unexpected_stream));
                    return {consumed,
                            transport::TransportError::callback_error, 0};
                }
                mark_remote_closed();
            } else {
                fail(session_error(V2SessionError::unexpected_event));
                return {consumed, transport::TransportError::callback_error,
                        0};
            }
            consumed += header.value.length;
        }
        return {consumed, transport::TransportError::none, 0};
    }

    void on_close(transport::ConnectionCloseReason, int system_error) override {
        std::lock_guard<std::mutex> lock(mutex);
        session_closed = true;
        if (failure.error == V2SessionError::none && system_error != 0) {
            failure = session_error(V2SessionError::transport_error);
            failure.transport_error = transport::TransportError::system_error;
            failure.system_error = system_error;
        }
        condition.notify_all();
    }

    V2SessionStatus drain_receive_queue() {
        for (;;) {
            const auto element = shared_memory.receive_queue().pop();
            if (!element) {
                if (element.error != shm::QueueError::empty) {
                    return queue_error(element.error);
                }
                if (shared_memory.receive_queue().mark_not_working()) {
                    return {};
                }
                continue;
            }
            if (!bind_or_validate_stream(element.value.sequence_id)) {
                return session_error(V2SessionError::unexpected_stream);
            }
            const auto state = element.value.status & 0xffU;
            if (state == stream_closed) {
                mark_remote_closed();
                continue;
            }
            if (state != stream_opened) {
                return session_error(V2SessionError::unexpected_event);
            }
            auto chain = shared_memory.buffer_pool().adopt_chain(
                element.value.buffer_offset);
            if (!chain) {
                auto status = session_error(V2SessionError::buffer_pool_error);
                status.buffer_pool_error = chain.error;
                return status;
            }
            auto reader = shm::make_buffer_reader(
                shared_memory.buffer_pool(), std::move(chain.value));
            if (!reader) {
                auto status = session_error(V2SessionError::buffer_io_error);
                status.buffer_io_error = reader.error;
                return status;
            }
            if (reader.value.remaining() >
                std::numeric_limits<std::size_t>::max()) {
                return session_error(V2SessionError::buffer_io_error);
            }
            const auto view = reader.value.read_bytes(
                static_cast<std::size_t>(reader.value.remaining()));
            if (!view) {
                auto status = session_error(V2SessionError::buffer_io_error);
                status.buffer_io_error = view.error;
                return status;
            }
            std::vector<std::uint8_t> message(view.value.size());
            if (!message.empty()) {
                std::memcpy(message.data(), view.value.data(), message.size());
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                messages.push_back(std::move(message));
            }
            condition.notify_all();
        }
    }

    void fail(const V2SessionStatus& status) {
        std::lock_guard<std::mutex> lock(mutex);
        if (failure.error == V2SessionError::none) {
            failure = status;
        }
        condition.notify_all();
    }

    void mark_remote_closed() {
        std::lock_guard<std::mutex> lock(mutex);
        remote_closed = true;
        condition.notify_all();
    }

    V2SharedMemory shared_memory;
    mutable std::mutex mutex{};
    std::condition_variable condition{};
    std::deque<std::vector<std::uint8_t>> messages{};
    V2SessionStatus failure{};
    bool local_closed{false};
    bool remote_closed{false};
    bool session_closed{false};
    std::uint32_t stream_id{0U};
};

V2ClientSession::V2ClientSession(
    std::shared_ptr<V2SingleStreamSessionState> state,
    std::shared_ptr<transport::EventConnection> connection) noexcept
    : state_(std::move(state)), connection_(std::move(connection)) {}

V2ClientSession::~V2ClientSession() { static_cast<void>(close()); }

V2ClientSession::operator bool() const noexcept {
    return state_ != nullptr && connection_ != nullptr;
}

bool V2ClientSession::is_open() const noexcept {
    return connection_ != nullptr && connection_->is_open();
}

V2SessionStatus V2ClientSession::send(const std::uint8_t* data,
                                      std::size_t size) {
    if (!state_ || !connection_ || data == nullptr || size == 0U) {
        return session_error(V2SessionError::invalid_argument);
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->local_closed || state_->remote_closed ||
            state_->session_closed) {
            return session_error(V2SessionError::closed);
        }
        if (!state_->failure) {
            return state_->failure;
        }
    }
    shm::BufferWriter writer(state_->shared_memory.buffer_pool());
    const auto written = writer.write_bytes(data, size);
    if (!written) {
        auto status = session_error(V2SessionError::buffer_io_error);
        status.buffer_io_error = written.error;
        return status;
    }
    const auto published = writer.publish();
    if (!published) {
        auto status = session_error(V2SessionError::buffer_pool_error);
        status.buffer_pool_error = published.error;
        return status;
    }
    const shm::QueueElement element{state_->current_stream_id(),
                                    published.value.root_offset,
                                    stream_opened};
    const auto queued = state_->shared_memory.send_queue().put(element);
    if (queued != shm::QueueError::none) {
        auto adopted = state_->shared_memory.buffer_pool().adopt_chain(
            published.value.root_offset);
        if (adopted) {
            static_cast<void>(state_->shared_memory.buffer_pool().recycle_chain(
                std::move(adopted.value)));
        }
        return queue_error(queued);
    }
    if (!state_->shared_memory.send_queue().mark_working()) {
        return {};
    }
    const auto polling = protocol::encode_header(
        {static_cast<std::uint32_t>(protocol::header_size),
         v2_protocol_version, protocol::EventType::polling});
    if (!polling) {
        auto status = session_error(V2SessionError::codec_error);
        status.codec_error = polling.error;
        return status;
    }
    const auto result = connection_->write(polling.value.data(),
                                           polling.value.size());
    return result ? V2SessionStatus{} : transport_error(result);
}

V2SessionStatus V2ClientSession::send(
    const std::vector<std::uint8_t>& data) {
    return send(data.data(), data.size());
}

V2ClientSession::MessageResult V2ClientSession::receive(
    std::chrono::milliseconds timeout) {
    if (!state_ || timeout.count() < 0) {
        return {{}, session_error(V2SessionError::invalid_argument)};
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    const auto ready = state_->condition.wait_for(lock, timeout, [&] {
        return !state_->messages.empty() || !state_->failure ||
               state_->remote_closed || state_->session_closed;
    });
    if (!ready) {
        return {{}, session_error(V2SessionError::timeout)};
    }
    if (!state_->messages.empty()) {
        auto message = std::move(state_->messages.front());
        state_->messages.pop_front();
        return {std::move(message), {}};
    }
    if (!state_->failure) {
        return {{}, state_->failure};
    }
    return {{}, session_error(V2SessionError::closed)};
}

V2SessionStatus V2ClientSession::close_stream() {
    if (!state_ || !connection_) {
        return session_error(V2SessionError::invalid_argument);
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->local_closed) {
            return {};
        }
        if (!state_->failure) {
            return state_->failure;
        }
        if (state_->session_closed) {
            return session_error(V2SessionError::closed);
        }
        state_->local_closed = true;
        if (state_->remote_closed) {
            return {};
        }
    }
    const auto queued = state_->shared_memory.send_queue().put(
        {state_->current_stream_id(), 0U, stream_closed});
    if (queued != shm::QueueError::none) {
        return queue_error(queued);
    }
    if (!state_->shared_memory.send_queue().mark_working()) {
        return {};
    }
    const auto polling = protocol::encode_header(
        {static_cast<std::uint32_t>(protocol::header_size),
         v2_protocol_version, protocol::EventType::polling});
    if (!polling) {
        auto status = session_error(V2SessionError::codec_error);
        status.codec_error = polling.error;
        return status;
    }
    const auto result = connection_->write(polling.value.data(),
                                           polling.value.size());
    return result ? V2SessionStatus{} : transport_error(result);
}

V2SessionStatus V2ClientSession::wait_remote_close(
    std::chrono::milliseconds timeout) {
    if (!state_ || timeout.count() < 0) {
        return session_error(V2SessionError::invalid_argument);
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->condition.wait_for(lock, timeout, [&] {
            return state_->remote_closed || !state_->failure ||
                   state_->session_closed;
        })) {
        return session_error(V2SessionError::timeout);
    }
    if (!state_->failure) {
        return state_->failure;
    }
    return state_->remote_closed ? V2SessionStatus{}
                                 : session_error(V2SessionError::closed);
}

V2SessionStatus V2ClientSession::close() noexcept {
    if (connection_) {
        const auto result = connection_->close();
        connection_.reset();
        state_.reset();
        if (result != transport::TransportError::none) {
            auto status = session_error(V2SessionError::transport_error);
            status.transport_error = result;
            return status;
        }
    } else {
        state_.reset();
    }
    return {};
}

V2ServerSession::V2ServerSession(
    std::shared_ptr<V2SingleStreamSessionState> state,
    std::shared_ptr<transport::EventConnection> connection) noexcept
    : state_(std::move(state)),
      connection_(std::move(connection)) {}

V2ServerSession::~V2ServerSession() { static_cast<void>(close()); }

V2ServerSession::operator bool() const noexcept {
    return state_ != nullptr && connection_ != nullptr;
}

bool V2ServerSession::is_open() const noexcept {
    return connection_ != nullptr && connection_->is_open();
}

std::uint32_t V2ServerSession::stream_id() const noexcept {
    return state_ ? state_->current_stream_id() : 0U;
}

V2SessionStatus
V2ServerSession::wait_stream(std::chrono::milliseconds timeout) {
    if (!state_ || timeout.count() < 0) {
        return session_error(V2SessionError::invalid_argument);
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->condition.wait_for(lock, timeout, [&] {
            return state_->stream_id != 0U || !state_->failure ||
                   state_->session_closed;
        })) {
        return session_error(V2SessionError::timeout);
    }
    if (!state_->failure) {
        return state_->failure;
    }
    return state_->stream_id != 0U ? V2SessionStatus{}
                                   : session_error(V2SessionError::closed);
}

V2SessionStatus V2ServerSession::send(const std::uint8_t* data,
                                      std::size_t size) {
    if (!state_ || !connection_ || data == nullptr || size == 0U) {
        return session_error(V2SessionError::invalid_argument);
    }
    std::uint32_t id = 0U;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->stream_id == 0U) {
            return session_error(V2SessionError::unexpected_stream);
        }
        if (state_->local_closed || state_->remote_closed ||
            state_->session_closed) {
            return session_error(V2SessionError::closed);
        }
        if (!state_->failure) {
            return state_->failure;
        }
        id = state_->stream_id;
    }
    shm::BufferWriter writer(state_->shared_memory.buffer_pool());
    const auto written = writer.write_bytes(data, size);
    if (!written) {
        auto status = session_error(V2SessionError::buffer_io_error);
        status.buffer_io_error = written.error;
        return status;
    }
    const auto published = writer.publish();
    if (!published) {
        auto status = session_error(V2SessionError::buffer_pool_error);
        status.buffer_pool_error = published.error;
        return status;
    }
    const auto queued = state_->shared_memory.send_queue().put(
        {id, published.value.root_offset, stream_opened});
    if (queued != shm::QueueError::none) {
        auto adopted = state_->shared_memory.buffer_pool().adopt_chain(
            published.value.root_offset);
        if (adopted) {
            static_cast<void>(state_->shared_memory.buffer_pool().recycle_chain(
                std::move(adopted.value)));
        }
        return queue_error(queued);
    }
    if (!state_->shared_memory.send_queue().mark_working()) {
        return {};
    }
    const auto polling = protocol::encode_header(
        {static_cast<std::uint32_t>(protocol::header_size), v2_protocol_version,
         protocol::EventType::polling});
    if (!polling) {
        auto status = session_error(V2SessionError::codec_error);
        status.codec_error = polling.error;
        return status;
    }
    const auto result =
        connection_->write(polling.value.data(), polling.value.size());
    return result ? V2SessionStatus{} : transport_error(result);
}

V2SessionStatus V2ServerSession::send(const std::vector<std::uint8_t>& data) {
    return send(data.data(), data.size());
}

V2ServerSession::MessageResult
V2ServerSession::receive(std::chrono::milliseconds timeout) {
    if (!state_ || timeout.count() < 0) {
        return {{}, session_error(V2SessionError::invalid_argument)};
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    const auto ready = state_->condition.wait_for(lock, timeout, [&] {
        return !state_->messages.empty() || !state_->failure ||
               state_->remote_closed || state_->session_closed;
    });
    if (!ready) {
        return {{}, session_error(V2SessionError::timeout)};
    }
    if (!state_->messages.empty()) {
        auto message = std::move(state_->messages.front());
        state_->messages.pop_front();
        return {std::move(message), {}};
    }
    if (!state_->failure) {
        return {{}, state_->failure};
    }
    return {{}, session_error(V2SessionError::closed)};
}

V2SessionStatus V2ServerSession::close_stream() {
    if (!state_ || !connection_) {
        return session_error(V2SessionError::invalid_argument);
    }
    std::uint32_t id = 0U;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->stream_id == 0U) {
            return session_error(V2SessionError::unexpected_stream);
        }
        if (state_->local_closed) {
            return {};
        }
        if (!state_->failure) {
            return state_->failure;
        }
        if (state_->session_closed) {
            return session_error(V2SessionError::closed);
        }
        state_->local_closed = true;
        if (state_->remote_closed) {
            return {};
        }
        id = state_->stream_id;
    }
    const auto queued =
        state_->shared_memory.send_queue().put({id, 0U, stream_closed});
    if (queued != shm::QueueError::none) {
        return queue_error(queued);
    }
    if (!state_->shared_memory.send_queue().mark_working()) {
        return {};
    }
    const auto polling = protocol::encode_header(
        {static_cast<std::uint32_t>(protocol::header_size), v2_protocol_version,
         protocol::EventType::polling});
    if (!polling) {
        auto status = session_error(V2SessionError::codec_error);
        status.codec_error = polling.error;
        return status;
    }
    const auto result =
        connection_->write(polling.value.data(), polling.value.size());
    return result ? V2SessionStatus{} : transport_error(result);
}

V2SessionStatus
V2ServerSession::wait_remote_close(std::chrono::milliseconds timeout) {
    if (!state_ || timeout.count() < 0) {
        return session_error(V2SessionError::invalid_argument);
    }
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->condition.wait_for(lock, timeout, [&] {
            return state_->remote_closed || !state_->failure ||
                   state_->session_closed;
        })) {
        return session_error(V2SessionError::timeout);
    }
    if (!state_->failure) {
        return state_->failure;
    }
    return state_->remote_closed ? V2SessionStatus{}
                                 : session_error(V2SessionError::closed);
}

V2SessionStatus V2ServerSession::close() noexcept {
    if (connection_) {
        const auto result = connection_->close();
        connection_.reset();
        state_.reset();
        if (result != transport::TransportError::none) {
            auto status = session_error(V2SessionError::transport_error);
            status.transport_error = result;
            return status;
        }
    } else {
        state_.reset();
    }
    return {};
}

const char* to_string(V2SessionError error) noexcept {
    switch (error) {
        case V2SessionError::none:
            return "none";
        case V2SessionError::invalid_argument:
            return "invalid argument";
        case V2SessionError::handshake_error:
            return "handshake error";
        case V2SessionError::dispatcher_error:
            return "dispatcher error";
        case V2SessionError::transport_error:
            return "transport error";
        case V2SessionError::codec_error:
            return "codec error";
        case V2SessionError::unexpected_event:
            return "unexpected event";
        case V2SessionError::unexpected_stream:
            return "unexpected stream";
        case V2SessionError::queue_error:
            return "queue error";
        case V2SessionError::buffer_pool_error:
            return "buffer pool error";
        case V2SessionError::buffer_io_error:
            return "buffer IO error";
        case V2SessionError::closed:
            return "closed";
        case V2SessionError::timeout:
            return "timeout";
    }
    return "unknown v2 session error";
}

V2ClientSessionResult start_v2_client_session(
    transport::ControlSocket&& socket, const V2ClientConfig& config,
    transport::EpollDispatcher& dispatcher) {
    if (!socket || !dispatcher) {
        return {{}, session_error(V2SessionError::invalid_argument)};
    }
    auto handshake = v2_client_handshake(socket, config);
    if (!handshake) {
        auto status = session_error(V2SessionError::handshake_error);
        status.handshake_status = handshake.status;
        return {{}, status};
    }
    auto state = std::make_shared<V2SingleStreamSessionState>(
        std::move(handshake.value), v2_single_stream_id);
    auto connection = dispatcher.add(std::move(socket), state);
    if (!connection) {
        auto status = session_error(V2SessionError::dispatcher_error);
        status.transport_error = connection.error;
        status.system_error = connection.system_error;
        return {{}, status};
    }
    return {V2ClientSession(std::move(state), std::move(connection.value)), {}};
}

V2ServerSessionResult
start_v2_server_session(transport::ControlSocket&& socket,
                        transport::EpollDispatcher& dispatcher,
                        std::uint32_t max_frame_length) {
    if (!socket || !dispatcher || max_frame_length < protocol::header_size) {
        return {{}, session_error(V2SessionError::invalid_argument)};
    }
    auto handshake = v2_server_handshake(socket, max_frame_length);
    if (!handshake) {
        auto status = session_error(V2SessionError::handshake_error);
        status.handshake_status = handshake.status;
        return {{}, status};
    }
    auto state = std::make_shared<V2SingleStreamSessionState>(
        std::move(handshake.value), 0U);
    auto connection = dispatcher.add(std::move(socket), state);
    if (!connection) {
        auto status = session_error(V2SessionError::dispatcher_error);
        status.transport_error = connection.error;
        status.system_error = connection.system_error;
        return {{}, status};
    }
    return {V2ServerSession(std::move(state), std::move(connection.value)), {}};
}

}  // namespace shmipc::core
