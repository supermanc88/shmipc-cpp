#pragma once

#include "protocol/control_codec.hpp"
#include "transport/control_socket.hpp"

#include <cstdint>

namespace shmipc::core {

inline constexpr std::uint8_t minimum_protocol_version = 2U;
inline constexpr std::uint8_t maximum_protocol_version = 3U;

enum class ProtocolVersionNegotiationError {
  none,
  invalid_argument,
  transport_error,
  codec_error,
  unexpected_header,
  unsupported_version,
};

struct ProtocolVersionNegotiationStatus {
  ProtocolVersionNegotiationError error{ProtocolVersionNegotiationError::none};
  int system_error{0};
  transport::TransportError transport_error{transport::TransportError::none};
  protocol::CodecError codec_error{protocol::CodecError::none};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == ProtocolVersionNegotiationError::none;
  }
};

struct ProtocolVersionNegotiationResult {
  std::uint8_t peer_max_version{0};
  std::uint8_t negotiated_version{0};
  ProtocolVersionNegotiationStatus status{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(status);
  }
};

[[nodiscard]] const char *
to_string(ProtocolVersionNegotiationError error) noexcept;

// Sends the fixed Go-compatible v3 offer, receives the server maximum, and
// selects min(local maximum, peer maximum). A peer maximum of v2 is a valid
// downgrade result; resource initialization is deliberately left to callers.
[[nodiscard]] ProtocolVersionNegotiationResult
negotiate_protocol_version_client(transport::ControlSocket &socket);

// Consumes a server-side first frame. The fixed Go server dispatches by the
// first header version, so only a v3 ExchangeProtoVersion frame reaches this
// state. The response advertises the local maximum (v3), not the selected
// value.
[[nodiscard]] ProtocolVersionNegotiationResult
negotiate_protocol_version_server(transport::ControlSocket &socket);

} // namespace shmipc::core
