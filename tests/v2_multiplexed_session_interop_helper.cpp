#include "core/v2_multiplexed_session.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool parse_port(const char *text, std::uint16_t &port) {
  char *end = nullptr;
  const auto value = std::strtoul(text, &end, 10);
  if (end == text || *end != '\0' || value == 0U || value > UINT16_MAX) {
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

void print_status(const shmipc::core::V2SessionStatus &status) {
  std::cerr << "session=" << shmipc::core::to_string(status.error)
            << " transport="
            << shmipc::transport::to_string(status.transport_error)
            << " queue=" << shmipc::shm::to_string(status.queue_error)
            << " pool=" << shmipc::shm::to_string(status.buffer_pool_error)
            << " buffer=" << shmipc::shm::to_string(status.buffer_io_error)
            << " errno=" << status.system_error << '\n';
}

std::vector<std::uint8_t> request(std::uint32_t id) {
  return std::vector<std::uint8_t>(2000U + id * 3000U,
                                   static_cast<std::uint8_t>(0x20U + id));
}

std::vector<std::uint8_t> response(std::uint32_t id) {
  return std::vector<std::uint8_t>(3000U + id * 2000U,
                                   static_cast<std::uint8_t>(0x40U + id));
}

std::vector<std::vector<std::uint8_t>> fallback_messages() {
  return {std::vector<std::uint8_t>(1024U, 0x31U),
          std::vector<std::uint8_t>(2U * 1024U * 1024U, 0x52U),
          std::vector<std::uint8_t>(257U, 0x73U)};
}

bool run_server(const std::string &host, std::uint16_t port) {
  auto socket = shmipc::transport::connect_tcp(host, port);
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  if (!socket || !dispatcher) {
    return false;
  }
  auto session = shmipc::core::start_v2_multiplexed_server_session(
      std::move(socket.value), dispatcher.value);
  if (!session) {
    print_status(session.status);
    return false;
  }
  for (std::size_t index = 0; index < 3U; ++index) {
    auto stream = session.value.accept_stream(5s);
    if (!stream || stream.value.id() < 2U || stream.value.id() > 4U) {
      print_status(stream.status);
      return false;
    }
    auto received = stream.value.receive(5s);
    if (!received || received.value != request(stream.value.id())) {
      print_status(received.status);
      return false;
    }
    if (!stream.value.send(response(stream.value.id())) ||
        !stream.value.wait_remote_close(5s) || !stream.value.close()) {
      return false;
    }
  }
  static_cast<void>(session.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

bool run_client(const std::string &host, std::uint16_t port,
                const std::string &queue_path, const std::string &buffer_path) {
  auto socket = shmipc::transport::connect_tcp(host, port);
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  if (!socket || !dispatcher) {
    return false;
  }
  const shmipc::core::V2ClientConfig config{
      queue_path, buffer_path, 64U, 1U << 20U, {{4096U, 60U}, {8192U, 40U}}};
  auto session = shmipc::core::start_v2_multiplexed_client_session(
      std::move(socket.value), config, dispatcher.value);
  if (!session) {
    print_status(session.status);
    return false;
  }
  std::vector<shmipc::core::V2Stream> streams;
  for (std::uint32_t id = 2U; id <= 4U; ++id) {
    auto stream = session.value.open_stream();
    if (!stream || stream.value.id() != id || !stream.value.send(request(id))) {
      print_status(stream.status);
      return false;
    }
    streams.push_back(std::move(stream.value));
  }
  for (auto &stream : streams) {
    auto received = stream.receive(5s);
    if (!received || received.value != response(stream.id())) {
      print_status(received.status);
      return false;
    }
    if (!stream.close() || !stream.wait_remote_close(5s)) {
      return false;
    }
  }
  static_cast<void>(session.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

bool run_fallback_server(const std::string &host, std::uint16_t port) {
  auto socket = shmipc::transport::connect_tcp(host, port);
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  if (!socket || !dispatcher) {
    return false;
  }
  auto session = shmipc::core::start_v2_multiplexed_server_session(
      std::move(socket.value), dispatcher.value);
  if (!session) {
    print_status(session.status);
    return false;
  }
  auto stream = session.value.accept_stream(5s);
  if (!stream) {
    print_status(stream.status);
    return false;
  }
  const auto expected = fallback_messages();
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    const auto received = stream.value.receive(5s);
    const auto expect_fallback = index != 0U;
    if (!received || received.value != expected[index] ||
        stream.value.is_fallback() != expect_fallback) {
      print_status(received.status);
      return false;
    }
  }
  if (!stream.value.send(std::vector<std::uint8_t>{0x7eU})) {
    return false;
  }
  if (!stream.value.wait_remote_close(5s) || !stream.value.close()) {
    return false;
  }
  static_cast<void>(session.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

bool run_fallback_client(const std::string &host, std::uint16_t port,
                         const std::string &queue_path,
                         const std::string &buffer_path) {
  auto socket = shmipc::transport::connect_tcp(host, port);
  auto dispatcher = shmipc::transport::start_epoll_dispatcher();
  if (!socket || !dispatcher) {
    return false;
  }
  const shmipc::core::V2ClientConfig config{
      queue_path, buffer_path, 16U, 1U << 20U, {{4096U, 100U}}};
  auto session = shmipc::core::start_v2_multiplexed_client_session(
      std::move(socket.value), config, dispatcher.value);
  if (!session) {
    print_status(session.status);
    return false;
  }
  auto stream = session.value.open_stream();
  if (!stream) {
    print_status(stream.status);
    return false;
  }
  const auto messages = fallback_messages();
  for (std::size_t index = 0U; index < messages.size(); ++index) {
    const auto expect_fallback = index != 0U;
    if (!stream.value.send(messages[index]) ||
        stream.value.is_fallback() != expect_fallback) {
      return false;
    }
  }
  const auto acknowledged = stream.value.receive(5s);
  if (!acknowledged ||
      acknowledged.value != std::vector<std::uint8_t>{0x7eU} ||
      !stream.value.is_fallback()) {
    print_status(acknowledged.status);
    return false;
  }
  if (!stream.value.wait_remote_close(5s) || !stream.value.close()) {
    return false;
  }
  static_cast<void>(session.value.close());
  static_cast<void>(dispatcher.value.stop());
  return true;
}

} // namespace

int main(int argc, char **argv) {
  std::uint16_t port = 0U;
  if ((argc != 4 && argc != 6) || !parse_port(argv[3], port)) {
    std::cerr << "usage: v2_multiplexed_session_interop_helper "
                 "{server|fallback-server} <host> <port>\n"
                 "   or: v2_multiplexed_session_interop_helper "
                 "{client|fallback-client} <host> <port> <queue> <buffer>\n";
    return 2;
  }
  const std::string mode(argv[1]);
  if (mode == "server" && argc == 4) {
    return run_server(argv[2], port) ? 0 : 1;
  }
  if (mode == "client" && argc == 6) {
    return run_client(argv[2], port, argv[4], argv[5]) ? 0 : 1;
  }
  if (mode == "fallback-server" && argc == 4) {
    return run_fallback_server(argv[2], port) ? 0 : 1;
  }
  if (mode == "fallback-client" && argc == 6) {
    return run_fallback_client(argv[2], port, argv[4], argv[5]) ? 0 : 1;
  }
  return 2;
}
