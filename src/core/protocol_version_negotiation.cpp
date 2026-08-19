#include "core/protocol_version_negotiation.hpp"

#include <algorithm>
#include <array>

namespace shmipc::core {
namespace {

ProtocolVersionNegotiationResult
failed(ProtocolVersionNegotiationStatus status) noexcept {
  return {0U, 0U, status};
}

ProtocolVersionNegotiationStatus
transport_failure(const transport::IoResult &result) noexcept {
  ProtocolVersionNegotiationStatus status{};
  status.error = ProtocolVersionNegotiationError::transport_error;
  status.system_error = result.system_error;
  status.transport_error = result.error;
  return status;
}

ProtocolVersionNegotiationStatus
codec_failure(protocol::CodecError error) noexcept {
  ProtocolVersionNegotiationStatus status{};
  status.error = ProtocolVersionNegotiationError::codec_error;
  status.codec_error = error;
  return status;
}

ProtocolVersionNegotiationStatus
simple_failure(ProtocolVersionNegotiationError error) noexcept {
  ProtocolVersionNegotiationStatus status{};
  status.error = error;
  return status;
}

protocol::BytesResult exchange_frame(std::uint8_t version) {
  return protocol::encode_header(
      {static_cast<std::uint32_t>(protocol::header_size), version,
       protocol::EventType::exchange_protocol_version});
}

ProtocolVersionNegotiationResult
read_exchange_response(transport::ControlSocket &socket) {
  std::array<std::uint8_t, protocol::header_size> bytes{};
  const auto read = socket.read_full(bytes.data(), bytes.size());
  if (!read) {
    return failed(transport_failure(read));
  }
  const auto header = protocol::decode_header(bytes.data(), bytes.size());
  if (!header) {
    return failed(codec_failure(header.error));
  }
  if (header.value.length != protocol::header_size ||
      header.value.type != protocol::EventType::exchange_protocol_version) {
    return failed(
        simple_failure(ProtocolVersionNegotiationError::unexpected_header));
  }
  if (header.value.version < minimum_protocol_version) {
    return failed(
        simple_failure(ProtocolVersionNegotiationError::unsupported_version));
  }
  return {header.value.version,
          std::min(maximum_protocol_version, header.value.version),
          {}};
}

} // namespace

const char *to_string(ProtocolVersionNegotiationError error) noexcept {
  switch (error) {
  case ProtocolVersionNegotiationError::none:
    return "none";
  case ProtocolVersionNegotiationError::invalid_argument:
    return "invalid argument";
  case ProtocolVersionNegotiationError::transport_error:
    return "transport error";
  case ProtocolVersionNegotiationError::codec_error:
    return "codec error";
  case ProtocolVersionNegotiationError::unexpected_header:
    return "unexpected header";
  case ProtocolVersionNegotiationError::unsupported_version:
    return "unsupported version";
  }
  return "unknown protocol version negotiation error";
}

ProtocolVersionNegotiationResult
negotiate_protocol_version_client(transport::ControlSocket &socket) {
  if (!socket) {
    return failed(
        simple_failure(ProtocolVersionNegotiationError::invalid_argument));
  }
  const auto frame = exchange_frame(maximum_protocol_version);
  if (!frame) {
    return failed(codec_failure(frame.error));
  }
  const auto written =
      socket.write_full(frame.value.data(), frame.value.size());
  if (!written) {
    return failed(transport_failure(written));
  }
  return read_exchange_response(socket);
}

ProtocolVersionNegotiationResult
negotiate_protocol_version_server(transport::ControlSocket &socket) {
  if (!socket) {
    return failed(
        simple_failure(ProtocolVersionNegotiationError::invalid_argument));
  }

  std::array<std::uint8_t, protocol::header_size> bytes{};
  const auto read = socket.read_full(bytes.data(), bytes.size());
  if (!read) {
    return failed(transport_failure(read));
  }
  const auto header = protocol::decode_header(bytes.data(), bytes.size());
  if (!header) {
    return failed(codec_failure(header.error));
  }
  if (header.value.length != protocol::header_size ||
      header.value.version != maximum_protocol_version ||
      header.value.type != protocol::EventType::exchange_protocol_version) {
    return failed(
        simple_failure(ProtocolVersionNegotiationError::unexpected_header));
  }

  const auto response = exchange_frame(maximum_protocol_version);
  if (!response) {
    return failed(codec_failure(response.error));
  }
  const auto written =
      socket.write_full(response.value.data(), response.value.size());
  if (!written) {
    return failed(transport_failure(written));
  }
  return {header.value.version, maximum_protocol_version, {}};
}

} // namespace shmipc::core
