#include "core/protocol_version_negotiation.hpp"

#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <utility>

#include <sys/socket.h>

namespace {

using shmipc::core::ProtocolVersionNegotiationError;
using shmipc::core::ProtocolVersionNegotiationResult;

struct SocketPair {
  shmipc::transport::ControlSocket client{};
  shmipc::transport::ControlSocket server{};
};

shmipc::transport::TransportResult<SocketPair> make_socket_pair() {
  int descriptors[2]{-1, -1};
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

std::array<std::uint8_t, shmipc::protocol::header_size>
raw_header(std::uint32_t length, std::uint16_t magic, std::uint8_t version,
           shmipc::protocol::EventType type) {
  const auto widened_magic = static_cast<std::uint32_t>(magic);
  return {{static_cast<std::uint8_t>((length >> 24U) & 0xffU),
           static_cast<std::uint8_t>((length >> 16U) & 0xffU),
           static_cast<std::uint8_t>((length >> 8U) & 0xffU),
           static_cast<std::uint8_t>(length & 0xffU),
           static_cast<std::uint8_t>((widened_magic >> 8U) & 0xffU),
           static_cast<std::uint8_t>(widened_magic & 0xffU), version,
           static_cast<std::uint8_t>(type)}};
}

bool request_is_v3_exchange(shmipc::transport::ControlSocket &socket) {
  std::array<std::uint8_t, shmipc::protocol::header_size> request{};
  const auto read = socket.read_full(request.data(), request.size());
  const auto decoded =
      read ? shmipc::protocol::decode_header(request.data(), request.size())
           : shmipc::protocol::HeaderResult{};
  return read && decoded &&
         decoded.value.length == shmipc::protocol::header_size &&
         decoded.value.version == shmipc::core::maximum_protocol_version &&
         decoded.value.type ==
             shmipc::protocol::EventType::exchange_protocol_version;
}

bool client_with_raw_response(
    const std::array<std::uint8_t, shmipc::protocol::header_size> &response,
    ProtocolVersionNegotiationResult &result) {
  auto sockets = make_socket_pair();
  if (!sockets) {
    return false;
  }
  auto peer = std::async(std::launch::async, [&] {
    return request_is_v3_exchange(sockets.value.server) &&
           sockets.value.server.write_full(response.data(), response.size());
  });
  result =
      shmipc::core::negotiate_protocol_version_client(sockets.value.client);
  return peer.get();
}

bool test_success() {
  auto sockets = make_socket_pair();
  if (!sockets) {
    return false;
  }
  auto server = std::async(std::launch::async, [&] {
    return shmipc::core::negotiate_protocol_version_server(
        sockets.value.server);
  });
  const auto client =
      shmipc::core::negotiate_protocol_version_client(sockets.value.client);
  const auto server_result = server.get();
  return client && server_result && client.peer_max_version == 3U &&
         client.negotiated_version == 3U &&
         server_result.peer_max_version == 3U &&
         server_result.negotiated_version == 3U;
}

bool test_client_version_selection() {
  ProtocolVersionNegotiationResult downgrade{};
  const auto v2 =
      raw_header(8U, shmipc::protocol::magic, 2U,
                 shmipc::protocol::EventType::exchange_protocol_version);
  if (!client_with_raw_response(v2, downgrade) || !downgrade ||
      downgrade.peer_max_version != 2U || downgrade.negotiated_version != 2U) {
    return false;
  }

  ProtocolVersionNegotiationResult future{};
  const auto v4 =
      raw_header(8U, shmipc::protocol::magic, 4U,
                 shmipc::protocol::EventType::exchange_protocol_version);
  return client_with_raw_response(v4, future) && future &&
         future.peer_max_version == 4U && future.negotiated_version == 3U;
}

bool test_client_rejects_bad_responses() {
  ProtocolVersionNegotiationResult result{};
  const auto unsupported =
      raw_header(8U, shmipc::protocol::magic, 1U,
                 shmipc::protocol::EventType::exchange_protocol_version);
  if (!client_with_raw_response(unsupported, result) || result ||
      result.status.error !=
          ProtocolVersionNegotiationError::unsupported_version) {
    return false;
  }

  const auto wrong_type = raw_header(8U, shmipc::protocol::magic, 3U,
                                     shmipc::protocol::EventType::polling);
  if (!client_with_raw_response(wrong_type, result) || result ||
      result.status.error !=
          ProtocolVersionNegotiationError::unexpected_header) {
    return false;
  }

  const auto wrong_length =
      raw_header(9U, shmipc::protocol::magic, 3U,
                 shmipc::protocol::EventType::exchange_protocol_version);
  if (!client_with_raw_response(wrong_length, result) || result ||
      result.status.error !=
          ProtocolVersionNegotiationError::unexpected_header) {
    return false;
  }

  const auto wrong_magic = raw_header(
      8U, 0x1234U, 3U, shmipc::protocol::EventType::exchange_protocol_version);
  return client_with_raw_response(wrong_magic, result) && !result &&
         result.status.error == ProtocolVersionNegotiationError::codec_error &&
         result.status.codec_error ==
             shmipc::protocol::CodecError::invalid_magic;
}

bool test_server_rejects_non_v3_first_frame() {
  auto sockets = make_socket_pair();
  if (!sockets) {
    return false;
  }
  const auto v2_exchange =
      raw_header(8U, shmipc::protocol::magic, 2U,
                 shmipc::protocol::EventType::exchange_protocol_version);
  if (!sockets.value.client.write_full(v2_exchange.data(),
                                       v2_exchange.size())) {
    return false;
  }
  const auto result =
      shmipc::core::negotiate_protocol_version_server(sockets.value.server);
  return !result && result.status.error ==
                        ProtocolVersionNegotiationError::unexpected_header;
}

bool test_transport_and_argument_failures() {
  shmipc::transport::ControlSocket invalid{};
  const auto bad_client =
      shmipc::core::negotiate_protocol_version_client(invalid);
  const auto bad_server =
      shmipc::core::negotiate_protocol_version_server(invalid);
  if (bad_client || bad_server ||
      bad_client.status.error !=
          ProtocolVersionNegotiationError::invalid_argument ||
      bad_server.status.error !=
          ProtocolVersionNegotiationError::invalid_argument) {
    return false;
  }

  auto sockets = make_socket_pair();
  if (!sockets ||
      sockets.value.client.close() != shmipc::transport::TransportError::none) {
    return false;
  }
  const auto eof =
      shmipc::core::negotiate_protocol_version_server(sockets.value.server);
  return !eof &&
         eof.status.error == ProtocolVersionNegotiationError::transport_error &&
         eof.status.transport_error ==
             shmipc::transport::TransportError::end_of_stream;
}

} // namespace

int main() {
  const bool ok = test_success() && test_client_version_selection() &&
                  test_client_rejects_bad_responses() &&
                  test_server_rejects_non_v3_first_frame() &&
                  test_transport_and_argument_failures();
  if (!ok) {
    std::cerr << "protocol version negotiation test failed\n";
  }
  return ok ? 0 : 1;
}
