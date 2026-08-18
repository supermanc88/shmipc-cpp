#include "core/v2_multiplexed_session.hpp"

#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace shmipc::core {

namespace {

constexpr std::uint32_t stream_opened = 0U;
constexpr std::uint32_t stream_closed = 1U;
constexpr std::size_t stream_close_frame_size = protocol::header_size + 4U;

V2SessionStatus make_session_error(V2SessionError error) {
  V2SessionStatus status{};
  status.error = error;
  return status;
}

V2SessionStatus make_transport_error(const transport::IoResult &result) {
  auto status = make_session_error(V2SessionError::transport_error);
  status.transport_error = result.error;
  status.system_error = result.system_error;
  return status;
}

V2SessionStatus make_queue_error(shm::QueueError error) {
  auto status = make_session_error(V2SessionError::queue_error);
  status.queue_error = error;
  return status;
}

std::uint32_t read_u32(const std::uint8_t *data) noexcept {
  return (static_cast<std::uint32_t>(data[0]) << 24U) |
         (static_cast<std::uint32_t>(data[1]) << 16U) |
         (static_cast<std::uint32_t>(data[2]) << 8U) |
         static_cast<std::uint32_t>(data[3]);
}

} // namespace

struct V2StreamState final {
  explicit V2StreamState(std::uint32_t stream_id) noexcept : id(stream_id) {}

  const std::uint32_t id;
  std::mutex mutex{};
  std::condition_variable condition{};
  std::deque<std::vector<std::uint8_t>> messages{};
  V2SessionStatus failure{};
  bool local_closed{false};
  bool remote_closed{false};
};

struct V2MultiplexedSessionState final : transport::ControlEventCallback {
  V2MultiplexedSessionState(V2SharedMemory &&memory, bool server) noexcept
      : shared_memory(std::move(memory)), is_server(server) {}

  transport::ConsumeResult on_data(const std::uint8_t *data, std::size_t size,
                                   transport::EventConnection &) override {
    std::size_t consumed = 0U;
    while (size - consumed >= protocol::header_size) {
      const auto header =
          protocol::decode_header(data + consumed, size - consumed);
      if (!header) {
        auto status = make_session_error(V2SessionError::codec_error);
        status.codec_error = header.error;
        fail(status);
        return {consumed, transport::TransportError::callback_error, 0};
      }
      if (header.value.length > size - consumed) {
        break;
      }
      if (header.value.version != v2_protocol_version) {
        fail(make_session_error(V2SessionError::unexpected_event));
        return {consumed, transport::TransportError::callback_error, 0};
      }
      V2SessionStatus status{};
      if (header.value.type == protocol::EventType::polling &&
          header.value.length == protocol::header_size) {
        status = drain_receive_queue();
      } else if (header.value.type == protocol::EventType::stream_close &&
                 header.value.length == stream_close_frame_size) {
        mark_remote_closed(read_u32(data + consumed + protocol::header_size));
      } else {
        status = make_session_error(V2SessionError::unexpected_event);
      }
      if (!status) {
        fail(status);
        return {consumed, transport::TransportError::callback_error, 0};
      }
      consumed += header.value.length;
    }
    return {consumed, transport::TransportError::none, 0};
  }

  void on_close(transport::ConnectionCloseReason, int system_error) override {
    auto status = make_session_error(V2SessionError::closed);
    if (system_error != 0) {
      status = make_session_error(V2SessionError::transport_error);
      status.transport_error = transport::TransportError::system_error;
      status.system_error = system_error;
    }
    fail(status);
  }

  std::shared_ptr<V2StreamState> find_stream(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto iterator = streams.find(id);
    return iterator == streams.end() ? nullptr : iterator->second;
  }

  std::shared_ptr<V2StreamState> find_or_accept_stream(std::uint32_t id) {
    if (id == 0U) {
      return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex);
    const auto existing = streams.find(id);
    if (existing != streams.end()) {
      return existing->second;
    }
    if (!is_server || closed || !failure) {
      return nullptr;
    }
    auto stream = std::make_shared<V2StreamState>(id);
    streams.emplace(id, stream);
    accepted.push_back(stream);
    condition.notify_all();
    return stream;
  }

  V2SessionStatus drain_receive_queue() {
    for (;;) {
      const auto element = shared_memory.receive_queue().pop();
      if (!element) {
        if (element.error != shm::QueueError::empty) {
          return make_queue_error(element.error);
        }
        if (shared_memory.receive_queue().mark_not_working()) {
          return {};
        }
        continue;
      }
      const auto state = element.value.status & 0xffU;
      if (state == stream_closed) {
        mark_remote_closed(element.value.sequence_id);
        continue;
      }
      if (state != stream_opened) {
        return make_session_error(V2SessionError::unexpected_event);
      }

      auto chain =
          shared_memory.buffer_pool().adopt_chain(element.value.buffer_offset);
      if (!chain) {
        auto status = make_session_error(V2SessionError::buffer_pool_error);
        status.buffer_pool_error = chain.error;
        return status;
      }
      auto reader = shm::make_buffer_reader(shared_memory.buffer_pool(),
                                            std::move(chain.value));
      if (!reader) {
        auto status = make_session_error(V2SessionError::buffer_io_error);
        status.buffer_io_error = reader.error;
        return status;
      }
      auto stream = find_or_accept_stream(element.value.sequence_id);
      if (!stream) {
        continue;
      }
      if (reader.value.remaining() > std::numeric_limits<std::size_t>::max()) {
        return make_session_error(V2SessionError::buffer_io_error);
      }
      const auto view = reader.value.read_bytes(
          static_cast<std::size_t>(reader.value.remaining()));
      if (!view) {
        auto status = make_session_error(V2SessionError::buffer_io_error);
        status.buffer_io_error = view.error;
        return status;
      }
      std::vector<std::uint8_t> message(view.value.size());
      if (!message.empty()) {
        std::memcpy(message.data(), view.value.data(), message.size());
      }
      {
        std::lock_guard<std::mutex> lock(stream->mutex);
        if (!stream->remote_closed) {
          stream->messages.push_back(std::move(message));
        }
      }
      stream->condition.notify_all();
    }
  }

  void mark_remote_closed(std::uint32_t id) {
    auto stream = find_stream(id);
    if (!stream) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(stream->mutex);
      stream->remote_closed = true;
    }
    stream->condition.notify_all();
  }

  void fail(const V2SessionStatus &status) {
    std::vector<std::shared_ptr<V2StreamState>> current_streams;
    {
      std::lock_guard<std::mutex> lock(mutex);
      closed = true;
      if (failure) {
        failure = status;
      }
      current_streams.reserve(streams.size());
      for (const auto &entry : streams) {
        current_streams.push_back(entry.second);
      }
    }
    condition.notify_all();
    for (const auto &stream : current_streams) {
      {
        std::lock_guard<std::mutex> lock(stream->mutex);
        if (stream->failure) {
          stream->failure = status;
        }
      }
      stream->condition.notify_all();
    }
  }

  V2SharedMemory shared_memory;
  const bool is_server;
  std::mutex mutex{};
  std::condition_variable condition{};
  std::unordered_map<std::uint32_t, std::shared_ptr<V2StreamState>> streams{};
  std::deque<std::shared_ptr<V2StreamState>> accepted{};
  V2SessionStatus failure{};
  bool closed{false};
  std::uint32_t next_stream_id{1U};
};

namespace {

V2SessionStatus
notify_peer(V2MultiplexedSessionState &state,
            const std::shared_ptr<transport::EventConnection> &connection) {
  if (!state.shared_memory.send_queue().mark_working()) {
    return {};
  }
  const auto polling = protocol::encode_header(
      {static_cast<std::uint32_t>(protocol::header_size), v2_protocol_version,
       protocol::EventType::polling});
  if (!polling) {
    auto status = make_session_error(V2SessionError::codec_error);
    status.codec_error = polling.error;
    return status;
  }
  const auto result =
      connection->write(polling.value.data(), polling.value.size());
  return result ? V2SessionStatus{} : make_transport_error(result);
}

void recycle_published(V2MultiplexedSessionState &state,
                       std::uint32_t root_offset) {
  auto chain = state.shared_memory.buffer_pool().adopt_chain(root_offset);
  if (chain) {
    static_cast<void>(state.shared_memory.buffer_pool().recycle_chain(
        std::move(chain.value)));
  }
}

} // namespace

V2Stream::V2Stream(
    std::shared_ptr<V2MultiplexedSessionState> session,
    std::shared_ptr<V2StreamState> stream,
    std::shared_ptr<transport::EventConnection> connection) noexcept
    : session_(std::move(session)), stream_(std::move(stream)),
      connection_(std::move(connection)) {}

V2Stream::~V2Stream() { static_cast<void>(close()); }

V2Stream &V2Stream::operator=(V2Stream &&other) noexcept {
  if (this != &other) {
    static_cast<void>(close());
    session_ = std::move(other.session_);
    stream_ = std::move(other.stream_);
    connection_ = std::move(other.connection_);
  }
  return *this;
}

V2Stream::operator bool() const noexcept {
  return session_ != nullptr && stream_ != nullptr && connection_ != nullptr;
}

std::uint32_t V2Stream::id() const noexcept {
  return stream_ ? stream_->id : 0U;
}

V2SessionStatus V2Stream::send(const std::uint8_t *data, std::size_t size) {
  if (!session_ || !stream_ || !connection_ || data == nullptr || size == 0U) {
    return make_session_error(V2SessionError::invalid_argument);
  }
  {
    std::lock_guard<std::mutex> lock(stream_->mutex);
    if (stream_->local_closed || stream_->remote_closed) {
      return make_session_error(V2SessionError::closed);
    }
    if (!stream_->failure) {
      return stream_->failure;
    }
  }
  shm::BufferWriter writer(session_->shared_memory.buffer_pool());
  const auto written = writer.write_bytes(data, size);
  if (!written) {
    auto status = make_session_error(V2SessionError::buffer_io_error);
    status.buffer_io_error = written.error;
    return status;
  }
  const auto published = writer.publish();
  if (!published) {
    auto status = make_session_error(V2SessionError::buffer_pool_error);
    status.buffer_pool_error = published.error;
    return status;
  }
  const auto queued = session_->shared_memory.send_queue().put(
      {stream_->id, published.value.root_offset, stream_opened});
  if (queued != shm::QueueError::none) {
    recycle_published(*session_, published.value.root_offset);
    return make_queue_error(queued);
  }
  return notify_peer(*session_, connection_);
}

V2SessionStatus V2Stream::send(const std::vector<std::uint8_t> &data) {
  return send(data.data(), data.size());
}

V2Stream::MessageResult V2Stream::receive(std::chrono::milliseconds timeout) {
  if (!stream_ || timeout.count() < 0) {
    return {{}, make_session_error(V2SessionError::invalid_argument)};
  }
  std::unique_lock<std::mutex> lock(stream_->mutex);
  if (!stream_->condition.wait_for(lock, timeout, [&] {
        return !stream_->messages.empty() || !stream_->failure ||
               stream_->local_closed || stream_->remote_closed;
      })) {
    return {{}, make_session_error(V2SessionError::timeout)};
  }
  if (stream_->local_closed) {
    return {{}, make_session_error(V2SessionError::closed)};
  }
  if (!stream_->messages.empty()) {
    auto message = std::move(stream_->messages.front());
    stream_->messages.pop_front();
    return {std::move(message), {}};
  }
  if (!stream_->failure) {
    return {{}, stream_->failure};
  }
  return {{}, make_session_error(V2SessionError::closed)};
}

V2SessionStatus V2Stream::close() {
  if (!session_ || !stream_ || !connection_) {
    return {};
  }
  bool send_close = false;
  V2SessionStatus failure{};
  {
    std::lock_guard<std::mutex> lock(stream_->mutex);
    if (!stream_->local_closed) {
      stream_->local_closed = true;
      stream_->messages.clear();
      send_close = stream_->failure && !stream_->remote_closed;
      if (!stream_->failure) {
        failure = stream_->failure;
      }
    }
  }
  stream_->condition.notify_all();
  if (!failure) {
    return failure;
  }
  V2SessionStatus status{};
  if (send_close) {
    const auto queued = session_->shared_memory.send_queue().put(
        {stream_->id, 0U, stream_closed});
    status = queued == shm::QueueError::none
                 ? notify_peer(*session_, connection_)
                 : make_queue_error(queued);
  }
  return status;
}

V2SessionStatus V2Stream::wait_remote_close(std::chrono::milliseconds timeout) {
  if (!stream_ || timeout.count() < 0) {
    return make_session_error(V2SessionError::invalid_argument);
  }
  std::unique_lock<std::mutex> lock(stream_->mutex);
  if (!stream_->condition.wait_for(lock, timeout, [&] {
        return stream_->remote_closed || !stream_->failure;
      })) {
    return make_session_error(V2SessionError::timeout);
  }
  if (!stream_->failure) {
    return stream_->failure;
  }
  return {};
}

V2MultiplexedClientSession::V2MultiplexedClientSession(
    std::shared_ptr<V2MultiplexedSessionState> state,
    std::shared_ptr<transport::EventConnection> connection) noexcept
    : state_(std::move(state)), connection_(std::move(connection)) {}

V2MultiplexedClientSession::~V2MultiplexedClientSession() {
  static_cast<void>(close());
}

V2MultiplexedClientSession::operator bool() const noexcept {
  return state_ != nullptr && connection_ != nullptr;
}

bool V2MultiplexedClientSession::is_open() const noexcept {
  return connection_ && connection_->is_open();
}

V2StreamResult V2MultiplexedClientSession::open_stream() {
  if (!state_ || !connection_) {
    return {{}, make_session_error(V2SessionError::invalid_argument)};
  }
  std::shared_ptr<V2StreamState> stream;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed || !state_->failure ||
        state_->next_stream_id == std::numeric_limits<std::uint32_t>::max()) {
      return {{},
              !state_->failure ? state_->failure
                               : make_session_error(V2SessionError::closed)};
    }
    const auto id = ++state_->next_stream_id;
    stream = std::make_shared<V2StreamState>(id);
    state_->streams.emplace(id, stream);
  }
  return {V2Stream(state_, std::move(stream), connection_), {}};
}

V2SessionStatus V2MultiplexedClientSession::close() noexcept {
  if (!connection_) {
    state_.reset();
    return {};
  }
  const auto result = connection_->close();
  connection_.reset();
  state_.reset();
  if (result != transport::TransportError::none) {
    auto status = make_session_error(V2SessionError::transport_error);
    status.transport_error = result;
    return status;
  }
  return {};
}

V2MultiplexedServerSession::V2MultiplexedServerSession(
    std::shared_ptr<V2MultiplexedSessionState> state,
    std::shared_ptr<transport::EventConnection> connection) noexcept
    : state_(std::move(state)), connection_(std::move(connection)) {}

V2MultiplexedServerSession::~V2MultiplexedServerSession() {
  static_cast<void>(close());
}

V2MultiplexedServerSession::operator bool() const noexcept {
  return state_ != nullptr && connection_ != nullptr;
}

bool V2MultiplexedServerSession::is_open() const noexcept {
  return connection_ && connection_->is_open();
}

V2StreamResult
V2MultiplexedServerSession::accept_stream(std::chrono::milliseconds timeout) {
  if (!state_ || !connection_ || timeout.count() < 0) {
    return {{}, make_session_error(V2SessionError::invalid_argument)};
  }
  std::unique_lock<std::mutex> lock(state_->mutex);
  if (!state_->condition.wait_for(lock, timeout, [&] {
        return !state_->accepted.empty() || !state_->failure || state_->closed;
      })) {
    return {{}, make_session_error(V2SessionError::timeout)};
  }
  if (!state_->accepted.empty()) {
    auto stream = std::move(state_->accepted.front());
    state_->accepted.pop_front();
    return {V2Stream(state_, std::move(stream), connection_), {}};
  }
  return {{},
          !state_->failure ? state_->failure
                           : make_session_error(V2SessionError::closed)};
}

V2SessionStatus V2MultiplexedServerSession::close() noexcept {
  if (!connection_) {
    state_.reset();
    return {};
  }
  const auto result = connection_->close();
  connection_.reset();
  state_.reset();
  if (result != transport::TransportError::none) {
    auto status = make_session_error(V2SessionError::transport_error);
    status.transport_error = result;
    return status;
  }
  return {};
}

V2MultiplexedClientSessionResult
start_v2_multiplexed_client_session(transport::ControlSocket &&socket,
                                    const V2ClientConfig &config,
                                    transport::EpollDispatcher &dispatcher) {
  if (!socket || !dispatcher) {
    return {{}, make_session_error(V2SessionError::invalid_argument)};
  }
  auto handshake = v2_client_handshake(socket, config);
  if (!handshake) {
    auto status = make_session_error(V2SessionError::handshake_error);
    status.handshake_status = handshake.status;
    return {{}, status};
  }
  auto state = std::make_shared<V2MultiplexedSessionState>(
      std::move(handshake.value), false);
  auto connection = dispatcher.add(std::move(socket), state);
  if (!connection) {
    auto status = make_session_error(V2SessionError::dispatcher_error);
    status.transport_error = connection.error;
    status.system_error = connection.system_error;
    return {{}, status};
  }
  return {
      V2MultiplexedClientSession(std::move(state), std::move(connection.value)),
      {}};
}

V2MultiplexedServerSessionResult
start_v2_multiplexed_server_session(transport::ControlSocket &&socket,
                                    transport::EpollDispatcher &dispatcher,
                                    std::uint32_t max_frame_length) {
  if (!socket || !dispatcher || max_frame_length < protocol::header_size) {
    return {{}, make_session_error(V2SessionError::invalid_argument)};
  }
  auto handshake = v2_server_handshake(socket, max_frame_length);
  if (!handshake) {
    auto status = make_session_error(V2SessionError::handshake_error);
    status.handshake_status = handshake.status;
    return {{}, status};
  }
  auto state = std::make_shared<V2MultiplexedSessionState>(
      std::move(handshake.value), true);
  auto connection = dispatcher.add(std::move(socket), state);
  if (!connection) {
    auto status = make_session_error(V2SessionError::dispatcher_error);
    status.transport_error = connection.error;
    status.system_error = connection.system_error;
    return {{}, status};
  }
  return {
      V2MultiplexedServerSession(std::move(state), std::move(connection.value)),
      {}};
}

} // namespace shmipc::core
