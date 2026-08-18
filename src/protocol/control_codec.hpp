#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shmipc::protocol {

inline constexpr std::size_t header_size = 8;
inline constexpr std::size_t fallback_prefix_size = 16;
inline constexpr std::uint16_t magic = 0x7758;
inline constexpr std::uint32_t default_max_frame_length = 64U * 1024U * 1024U;

enum class EventType : std::uint8_t {
    share_memory_by_file_path = 0,
    polling = 1,
    stream_close = 2,
    fallback_data = 3,
    exchange_protocol_version = 4,
    share_memory_by_memfd = 5,
    ack_share_memory = 6,
    ack_ready_receive_fd = 7,
    hot_restart = 8,
    hot_restart_ack = 9,
};

enum class CodecError {
    none,
    truncated_header,
    truncated_body,
    invalid_length,
    invalid_magic,
    invalid_version,
    invalid_event_type,
    invalid_event_for_payload,
    field_too_long,
    trailing_bytes,
};

struct Header {
    std::uint32_t length{0};
    std::uint8_t version{0};
    EventType type{EventType::share_memory_by_file_path};
};

struct SharedMemoryMetadata {
    Header header;
    std::string queue_path;
    std::string buffer_path;
};

struct FallbackData {
    Header header;
    std::uint32_t stream_id{0};
    std::uint32_t raw_status{0};
    std::uint8_t stream_state{0};
    std::vector<std::uint8_t> payload;
};

template <typename T>
struct CodecResult {
    T value{};
    CodecError error{CodecError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == CodecError::none;
    }
};

using HeaderResult = CodecResult<Header>;
using BytesResult = CodecResult<std::vector<std::uint8_t>>;
using MetadataResult = CodecResult<SharedMemoryMetadata>;
using FallbackResult = CodecResult<FallbackData>;

[[nodiscard]] const char* to_string(EventType type) noexcept;
[[nodiscard]] const char* to_string(CodecError error) noexcept;

[[nodiscard]] BytesResult encode_header(const Header& header);
[[nodiscard]] HeaderResult decode_header(
    const std::uint8_t* data, std::size_t size,
    std::uint32_t max_frame_length = default_max_frame_length) noexcept;

[[nodiscard]] BytesResult encode_shared_memory_metadata(
    std::uint8_t version, EventType type, const std::string& queue_path,
    const std::string& buffer_path);
[[nodiscard]] MetadataResult decode_shared_memory_metadata(
    const std::uint8_t* frame, std::size_t size,
    std::uint32_t max_frame_length = default_max_frame_length);

[[nodiscard]] BytesResult encode_fallback_data(
    std::uint8_t version, std::uint32_t stream_id, std::uint32_t status,
    const std::vector<std::uint8_t>& payload);
[[nodiscard]] FallbackResult decode_fallback_data(
    const std::uint8_t* frame, std::size_t size,
    std::uint32_t max_frame_length = default_max_frame_length);

}  // namespace shmipc::protocol
