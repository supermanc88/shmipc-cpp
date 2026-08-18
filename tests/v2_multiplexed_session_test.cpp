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
         server.accept_stream(1ms).status.error ==
             shmipc::core::V2SessionError::invalid_argument;
}

} // namespace

int main() {
  if (!test_three_stream_round_trip()) {
    std::cerr << "v2 multiplexed three-stream round-trip test failed\n";
    return 1;
  }
  if (!test_invalid_state()) {
    std::cerr << "v2 multiplexed invalid-state test failed\n";
    return 1;
  }
  return 0;
}
