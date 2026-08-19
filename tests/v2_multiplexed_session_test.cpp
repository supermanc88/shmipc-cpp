#include "core/v2_multiplexed_session.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct SocketPair {
  shmipc::transport::ControlSocket client{};
  shmipc::transport::ControlSocket server{};
};

shmipc::transport::TransportResult<SocketPair> make_socket_pair() {
  int descriptors[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
    return {{}, shmipc::transport::TransportError::system_error, errno};
  }
  auto client = shmipc::transport::adopt_control_socket(descriptors[0]);
  auto server = shmipc::transport::adopt_control_socket(descriptors[1]);
  if (!client || !server) {
    return {{},
            shmipc::transport::TransportError::system_error,
            client ? server.system_error : client.system_error};
  }
  return {{std::move(client.value), std::move(server.value)},
          shmipc::transport::TransportError::none,
          0};
}

struct TestDirectory {
  std::array<char, 64> storage{};
  std::string path{};

  bool create() {
    const std::string pattern = "/tmp/shmipc-v2-mux.XXXXXX";
    std::copy(pattern.begin(), pattern.end(), storage.begin());
    auto *const directory = ::mkdtemp(storage.data());
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

bool test_three_stream_round_trip() {
  auto sockets = make_socket_pair();
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  TestDirectory directory;
  if (!dispatcher) {
    return dispatcher.error == shmipc::transport::TransportError::unsupported;
  }
  if (!sockets || !directory.create()) {
    return false;
  }
  const shmipc::core::V2ClientConfig config{directory.path + "/queue",
                                            directory.path + "/buffer",
                                            64U,
                                            1U << 20U,
                                            {{4096U, 60U}, {8192U, 40U}}};

  std::promise<shmipc::core::V2MultiplexedServerSessionResult> promise;
  auto future = promise.get_future();
  std::thread server_thread([&] {
    promise.set_value(shmipc::core::start_v2_multiplexed_server_session(
        std::move(sockets.value.server), dispatcher.value));
  });
  auto client = shmipc::core::start_v2_multiplexed_client_session(
      std::move(sockets.value.client), config, dispatcher.value);
  auto server = future.get();
  server_thread.join();
  if (!client || !server ||
      server.value.accept_stream(1ms).status.error !=
          shmipc::core::V2SessionError::timeout) {
    return false;
  }

  std::array<shmipc::core::V2Stream, 3> clients{};
  std::unordered_map<std::uint32_t, std::vector<std::uint8_t>> requests;
  for (std::size_t index = 0; index < clients.size(); ++index) {
    auto stream = client.value.open_stream();
    if (!stream || stream.value.id() != index + 2U) {
      return false;
    }
    requests.emplace(
        stream.value.id(),
        std::vector<std::uint8_t>(2000U + index * 7000U,
                                  static_cast<std::uint8_t>(0x21U + index)));
    clients[index] = std::move(stream.value);
  }

  std::array<bool, 3> sent{};
  std::array<std::thread, 3> senders{};
  for (std::size_t index = 0; index < clients.size(); ++index) {
    senders[index] = std::thread([&, index] {
      sent[index] = static_cast<bool>(
          clients[index].send(requests.at(clients[index].id())));
    });
  }
  for (auto &sender : senders) {
    sender.join();
  }
  if (std::find(sent.begin(), sent.end(), false) != sent.end()) {
    return false;
  }

  std::unordered_map<std::uint32_t, shmipc::core::V2Stream> servers;
  for (std::size_t index = 0; index < clients.size(); ++index) {
    auto accepted = server.value.accept_stream(5s);
    if (!accepted || requests.count(accepted.value.id()) != 1U) {
      return false;
    }
    const auto id = accepted.value.id();
    auto message = accepted.value.receive(5s);
    if (!message || message.value != requests.at(id)) {
      return false;
    }
    servers.emplace(id, std::move(accepted.value));
  }

  auto blocked_read =
      std::async(std::launch::async, [&] { return clients[0].receive(5s); });
  std::this_thread::sleep_for(20ms);
  clients[0].set_read_deadline(shmipc::core::V2Stream::Clock::now());
  if (blocked_read.wait_for(500ms) != std::future_status::ready ||
      blocked_read.get().status.error !=
          shmipc::core::V2SessionError::timeout ||
      clients[0].receive(5s).status.error !=
          shmipc::core::V2SessionError::timeout) {
    return false;
  }
  clients[0].set_read_deadline(std::nullopt);

  for (auto &entry : servers) {
    const std::vector<std::uint8_t> response(
        3000U + entry.first * 1000U,
        static_cast<std::uint8_t>(0x40U + entry.first));
    if (!entry.second.send(response)) {
      return false;
    }
    const auto client_index = static_cast<std::size_t>(entry.first - 2U);
    auto received = clients[client_index].receive(5s);
    if (!received || received.value != response) {
      return false;
    }
  }

  if (!clients[0].close() ||
      clients[0].receive(1ms).status.error !=
          shmipc::core::V2SessionError::closed ||
      !servers.at(2U).wait_remote_close(5s) || !servers.at(2U).close()) {
    return false;
  }
  for (std::uint32_t id = 3U; id <= 4U; ++id) {
    const auto client_index = static_cast<std::size_t>(id - 2U);
    if (!servers.at(id).close() ||
        !clients[client_index].wait_remote_close(5s) ||
        !clients[client_index].close()) {
      return false;
    }
  }

  for (auto &stream : clients) {
    stream = {};
  }
  servers.clear();

  if (!client.value.close() || !server.value.close() ||
      dispatcher.value.stop() != shmipc::transport::TransportError::none) {
    return false;
  }
  return ::access(config.queue_path.c_str(), F_OK) != 0 &&
         ::access(config.buffer_path.c_str(), F_OK) != 0;
}

bool recycle_element(shmipc::core::V2SharedMemory &memory,
                     const shmipc::shm::QueueElement &element) {
  auto chain = memory.buffer_pool().adopt_chain(element.buffer_offset);
  return chain && memory.buffer_pool().recycle_chain(std::move(chain.value)) ==
                      shmipc::shm::BufferPoolError::none;
}

bool test_buffer_exhaustion_sticky_fallback() {
  auto sockets = make_socket_pair();
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  TestDirectory directory;
  if (!dispatcher) {
    return dispatcher.error == shmipc::transport::TransportError::unsupported;
  }
  if (!sockets || !directory.create()) {
    return false;
  }
  const shmipc::core::V2ClientConfig config{directory.path + "/queue",
                                            directory.path + "/buffer",
                                            16U,
                                            64U * 1024U,
                                            {{4096U, 100U}}};

  std::promise<shmipc::core::V2MultiplexedServerSessionResult> promise;
  auto future = promise.get_future();
  std::thread server_thread([&] {
    promise.set_value(shmipc::core::start_v2_multiplexed_server_session(
        std::move(sockets.value.server), dispatcher.value));
  });
  auto client = shmipc::core::start_v2_multiplexed_client_session(
      std::move(sockets.value.client), config, dispatcher.value);
  auto server = future.get();
  server_thread.join();
  if (!client || !server) {
    return false;
  }

  auto opened = client.value.open_stream();
  if (!opened) {
    return false;
  }
  auto client_stream = std::move(opened.value);
  const std::vector<std::uint8_t> shared(1024U, 0x31U);
  const std::vector<std::uint8_t> exhausted(128U * 1024U, 0x52U);
  const std::vector<std::uint8_t> sticky(257U, 0x73U);
  if (!client_stream.send(shared) || client_stream.is_fallback() ||
      !client_stream.send(exhausted) || !client_stream.is_fallback() ||
      client.value.is_healthy() ||
      client.value.open_stream().status.error !=
          shmipc::core::V2SessionError::unhealthy ||
      !client_stream.send(sticky) || !client_stream.is_fallback()) {
    return false;
  }

  auto accepted = server.value.accept_stream(5s);
  if (!accepted) {
    return false;
  }
  auto server_stream = std::move(accepted.value);
  const auto first = server_stream.receive(5s);
  if (!first || first.value != shared || server_stream.is_fallback()) {
    return false;
  }
  const auto second = server_stream.receive(5s);
  if (!second || second.value != exhausted || !server_stream.is_fallback() ||
      server.value.is_healthy()) {
    return false;
  }
  const auto third = server_stream.receive(5s);
  if (!third || third.value != sticky || !server_stream.is_fallback()) {
    return false;
  }

  static_cast<void>(client_stream.close());
  static_cast<void>(server_stream.close());
  static_cast<void>(client.value.close());
  static_cast<void>(server.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

bool test_queue_full_retry_and_close_fallback() {
  auto sockets = make_socket_pair();
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  TestDirectory directory;
  if (!dispatcher) {
    return dispatcher.error == shmipc::transport::TransportError::unsupported;
  }
  if (!sockets || !directory.create()) {
    return false;
  }
  const shmipc::core::V2ClientConfig config{directory.path + "/queue",
                                            directory.path + "/buffer",
                                            8U,
                                            1U << 20U,
                                            {{4096U, 60U}, {8192U, 40U}}};

  std::promise<shmipc::core::V2HandshakeResult> promise;
  auto future = promise.get_future();
  std::thread server_thread([&] {
    promise.set_value(shmipc::core::v2_server_handshake(sockets.value.server));
  });
  auto client = shmipc::core::start_v2_multiplexed_client_session(
      std::move(sockets.value.client), config, dispatcher.value);
  auto server = future.get();
  server_thread.join();
  if (!client || !server) {
    return false;
  }
  auto opened = client.value.open_stream();
  if (!opened) {
    return false;
  }
  auto stream = std::move(opened.value);
  const std::vector<std::uint8_t> payload(31U, 0x7aU);
  for (std::size_t index = 0U; index < 8U; ++index) {
    if (!stream.send(payload)) {
      return false;
    }
  }

  stream.set_write_deadline(shmipc::core::V2Stream::Clock::now());
  const auto timeout_start = shmipc::core::V2Stream::Clock::now();
  if (stream.send(payload).error != shmipc::core::V2SessionError::timeout ||
      shmipc::core::V2Stream::Clock::now() - timeout_start > 100ms ||
      stream.is_fallback()) {
    return false;
  }
  stream.set_write_deadline(std::nullopt);

  bool recycled = false;
  std::thread consumer([&] {
    std::this_thread::sleep_for(25ms);
    const auto element = server.value.receive_queue().pop();
    recycled = element && recycle_element(server.value, element.value);
  });
  const auto retry_start = shmipc::core::V2Stream::Clock::now();
  const auto retried = stream.send(payload);
  const auto retry_elapsed = shmipc::core::V2Stream::Clock::now() - retry_start;
  consumer.join();
  if (!retried || !recycled || retry_elapsed < 20ms || retry_elapsed > 150ms) {
    return false;
  }

  std::array<std::uint8_t, shmipc::protocol::header_size> polling{};
  if (!sockets.value.server.read_full(polling.data(), polling.size())) {
    return false;
  }
  auto blocked_send =
      std::async(std::launch::async, [&] { return stream.send(payload); });
  std::this_thread::sleep_for(20ms);
  if (!stream.close() ||
      blocked_send.get().error != shmipc::core::V2SessionError::closed) {
    return false;
  }
  std::array<std::uint8_t, shmipc::protocol::header_size + 4U> close_frame{};
  if (!sockets.value.server.read_full(close_frame.data(), close_frame.size())) {
    return false;
  }
  const auto header =
      shmipc::protocol::decode_header(close_frame.data(), close_frame.size());
  const auto close_id = (static_cast<std::uint32_t>(close_frame[8]) << 24U) |
                        (static_cast<std::uint32_t>(close_frame[9]) << 16U) |
                        (static_cast<std::uint32_t>(close_frame[10]) << 8U) |
                        static_cast<std::uint32_t>(close_frame[11]);
  if (!header ||
      header.value.type != shmipc::protocol::EventType::stream_close ||
      header.value.length != close_frame.size() || close_id != stream.id()) {
    return false;
  }
  if (!client.value.is_healthy()) {
    return false;
  }

  for (std::size_t index = 0U; index < 8U; ++index) {
    const auto element = server.value.receive_queue().pop();
    if (!element || !recycle_element(server.value, element.value)) {
      return false;
    }
  }
  if (!server.value.receive_queue().mark_not_working()) {
    return false;
  }
  stream = {};
  static_cast<void>(client.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

bool test_session_failure_propagation() {
  auto sockets = make_socket_pair();
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  TestDirectory directory;
  if (!dispatcher) {
    return dispatcher.error == shmipc::transport::TransportError::unsupported;
  }
  if (!sockets || !directory.create()) {
    return false;
  }
  const shmipc::core::V2ClientConfig config{directory.path + "/queue",
                                            directory.path + "/buffer",
                                            64U,
                                            1U << 20U,
                                            {{4096U, 60U}, {8192U, 40U}}};
  std::promise<shmipc::core::V2MultiplexedServerSessionResult> promise;
  auto future = promise.get_future();
  std::thread server_thread([&] {
    promise.set_value(shmipc::core::start_v2_multiplexed_server_session(
        std::move(sockets.value.server), dispatcher.value));
  });
  auto client = shmipc::core::start_v2_multiplexed_client_session(
      std::move(sockets.value.client), config, dispatcher.value);
  auto server = future.get();
  server_thread.join();
  auto first =
      client ? client.value.open_stream() : shmipc::core::V2StreamResult{};
  auto second =
      client ? client.value.open_stream() : shmipc::core::V2StreamResult{};
  if (!client || !server || !first || !second) {
    return false;
  }
  auto blocked_accept = std::async(
      std::launch::async, [&] { return server.value.accept_stream(5s); });
  if (!first.value.close() || !client.value.close()) {
    return false;
  }
  const auto first_failure = first.value.receive(5s);
  const auto second_failure = second.value.receive(5s);
  if (blocked_accept.get().status.error !=
          shmipc::core::V2SessionError::closed ||
      first_failure.status.error != shmipc::core::V2SessionError::closed ||
      first.value.wait_remote_close(5s).error !=
          shmipc::core::V2SessionError::closed ||
      second_failure.status.error != shmipc::core::V2SessionError::closed ||
      client.value.open_stream().status.error !=
          shmipc::core::V2SessionError::invalid_argument) {
    return false;
  }
  first.value = {};
  second.value = {};
  static_cast<void>(server.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

bool test_invalid_state() {
  shmipc::core::V2Stream stream;
  shmipc::core::V2MultiplexedClientSession client;
  shmipc::core::V2MultiplexedServerSession server;
  const std::vector<std::uint8_t> data{1U};
  return stream.id() == 0U &&
         stream.send(data).error ==
             shmipc::core::V2SessionError::invalid_argument &&
         stream.receive(1ms).status.error ==
             shmipc::core::V2SessionError::invalid_argument &&
         client.open_stream().status.error ==
             shmipc::core::V2SessionError::invalid_argument &&
         !client.is_healthy() && !server.is_healthy() &&
         server.accept_stream(1ms).status.error ==
             shmipc::core::V2SessionError::invalid_argument;
}

bool test_session_circuit_breaker() {
  shmipc::core::SessionCircuitBreaker breaker(200ms);
  if (!breaker.is_healthy()) {
    return false;
  }
  breaker.open();
  if (breaker.is_healthy()) {
    return false;
  }
  std::this_thread::sleep_for(50ms);
  breaker.open();
  std::this_thread::sleep_for(170ms);
  if (!breaker.is_healthy()) {
    return false;
  }
  breaker.open();
  return !breaker.is_healthy();
}

} // namespace

int main() {
  if (!test_session_circuit_breaker()) {
    std::cerr << "session circuit breaker test failed\n";
    return 1;
  }
  if (!test_three_stream_round_trip()) {
    std::cerr << "v2 multiplexed three-stream round-trip test failed\n";
    return 1;
  }
  if (!test_invalid_state()) {
    std::cerr << "v2 multiplexed invalid-state test failed\n";
    return 1;
  }
  if (!test_queue_full_retry_and_close_fallback()) {
    std::cerr << "v2 multiplexed queue-full/deadline test failed\n";
    return 1;
  }
  if (!test_buffer_exhaustion_sticky_fallback()) {
    std::cerr << "v2 multiplexed sticky fallback test failed\n";
    return 1;
  }
  if (!test_session_failure_propagation()) {
    std::cerr << "v2 multiplexed failure propagation test failed\n";
    return 1;
  }
  return 0;
}
