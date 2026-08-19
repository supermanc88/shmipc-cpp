#include "core/protocol_version_negotiation.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool parse_port(const char *text, std::uint16_t &port) {
  char *end = nullptr;
  const auto value = std::strtoul(text, &end, 10);
  if (end == text || *end != '\0' || value == 0U || value > UINT16_MAX) {
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

void print_result(
    const shmipc::core::ProtocolVersionNegotiationResult &result) {
  std::cerr << "negotiation=" << shmipc::core::to_string(result.status.error)
            << " transport="
            << shmipc::transport::to_string(result.status.transport_error)
            << " codec="
            << shmipc::protocol::to_string(result.status.codec_error)
            << " peer=" << static_cast<unsigned>(result.peer_max_version)
            << " selected=" << static_cast<unsigned>(result.negotiated_version)
            << " errno=" << result.status.system_error << '\n';
}

bool run(const std::string &role, const std::string &host, std::uint16_t port) {
  auto socket = shmipc::transport::connect_tcp(host, port);
  if (!socket) {
    std::cerr << "connect=" << shmipc::transport::to_string(socket.error)
              << " errno=" << socket.system_error << '\n';
    return false;
  }
  const auto result =
      role == "client"
          ? shmipc::core::negotiate_protocol_version_client(socket.value)
          : shmipc::core::negotiate_protocol_version_server(socket.value);
  if (!result || result.peer_max_version != 3U ||
      result.negotiated_version != 3U) {
    print_result(result);
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  std::uint16_t port = 0;
  const std::string role = argc > 1 ? argv[1] : "";
  if (argc != 4 || (role != "client" && role != "server") ||
      !parse_port(argv[3], port)) {
    std::cerr << "usage: protocol_version_negotiation_interop_helper "
                 "<client|server> <host> <port>\n";
    return 2;
  }
  return run(role, argv[2], port) ? 0 : 1;
}
