#include "protocol/control_codec.hpp"

#include <limits>
#include <utility>

namespace shmipc::protocol {
namespace {

constexpr std::uint8_t max_event_type = 9;

std::uint16_t read_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8U) |
        static_cast<std::uint16_t>(data[1]));
}

std::uint32_t read_u32(const std::uint8_t* data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 24U) |
           (static_cast<std::uint32_t>(data[1]) << 16U) |
           (static_cast<std::uint32_t>(data[2]) << 8U) |
           static_cast<std::uint32_t>(data[3]);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    const auto widened = static_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::uint8_t>((widened >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(widened & 0xffU));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

bool is_valid_event(EventType type) noexcept {
    return static_cast<std::uint8_t>(type) <= max_event_type;
}

bool is_metadata_event(EventType type) noexcept {
    return type == EventType::share_memory_by_file_path ||
           type == EventType::share_memory_by_memfd;
}

CodecError validate_complete_frame(const Header& header, std::size_t size) noexcept {
    if (static_cast<std::size_t>(header.length) > size) {
        return CodecError::truncated_body;
    }
    if (static_cast<std::size_t>(header.length) < size) {
        return CodecError::trailing_bytes;
    }
    return CodecError::none;
}

}  // namespace

const char* to_string(EventType type) noexcept {
    switch (type) {
        case EventType::share_memory_by_file_path:
            return "ShareMemoryByFilePath";
        case EventType::polling:
            return "Polling";
        case EventType::stream_close:
            return "StreamClose";
        case EventType::fallback_data:
            return "FallbackData";
        case EventType::exchange_protocol_version:
            return "ExchangeProtoVersion";
        case EventType::share_memory_by_memfd:
            return "ShareMemoryByMemfd";
        case EventType::ack_share_memory:
            return "AckShareMemory";
        case EventType::ack_ready_receive_fd:
            return "AckReadyRecvFD";
        case EventType::hot_restart:
            return "HotRestart";
        case EventType::hot_restart_ack:
            return "HotRestartAck";
    }
    return "<UNSET>";
}

const char* to_string(CodecError error) noexcept {
    switch (error) {
        case CodecError::none:
            return "none";
        case CodecError::truncated_header:
            return "truncated header";
        case CodecError::truncated_body:
            return "truncated body";
        case CodecError::invalid_length:
            return "invalid length";
        case CodecError::invalid_magic:
            return "invalid magic";
        case CodecError::invalid_version:
            return "invalid version";
        case CodecError::invalid_event_type:
            return "invalid event type";
        case CodecError::invalid_event_for_payload:
            return "invalid event for payload";
        case CodecError::field_too_long:
            return "field too long";
        case CodecError::trailing_bytes:
            return "trailing bytes";
    }
    return "unknown codec error";
}

BytesResult encode_header(const Header& header) {
    if (header.length < header_size ||
        header.length > default_max_frame_length) {
        return {{}, CodecError::invalid_length};
    }
    if (header.version == 0) {
        return {{}, CodecError::invalid_version};
    }
    if (!is_valid_event(header.type)) {
        return {{}, CodecError::invalid_event_type};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(header_size);
    append_u32(bytes, header.length);
    append_u16(bytes, magic);
    bytes.push_back(header.version);
    bytes.push_back(static_cast<std::uint8_t>(header.type));
    return {std::move(bytes), CodecError::none};
}

HeaderResult decode_header(const std::uint8_t* data, std::size_t size,
                           std::uint32_t max_frame_length) noexcept {
    if (data == nullptr || size < header_size) {
        return {{}, CodecError::truncated_header};
    }

    Header header;
    header.length = read_u32(data);
    if (header.length < header_size || header.length > max_frame_length) {
        return {{}, CodecError::invalid_length};
    }
    if (read_u16(data + 4) != magic) {
        return {{}, CodecError::invalid_magic};
    }
    header.version = data[6];
    if (header.version == 0) {
        return {{}, CodecError::invalid_version};
    }
    if (data[7] > max_event_type) {
        return {{}, CodecError::invalid_event_type};
    }
    header.type = static_cast<EventType>(data[7]);
    return {header, CodecError::none};
}

BytesResult encode_shared_memory_metadata(std::uint8_t version, EventType type,
                                          const std::string& queue_path,
                                          const std::string& buffer_path) {
    if (!is_metadata_event(type)) {
        return {{}, CodecError::invalid_event_for_payload};
    }
    if (queue_path.size() > std::numeric_limits<std::uint16_t>::max() ||
        buffer_path.size() > std::numeric_limits<std::uint16_t>::max()) {
        return {{}, CodecError::field_too_long};
    }
    const std::size_t total_size =
        header_size + 2U + queue_path.size() + 2U + buffer_path.size();
    if (total_size > default_max_frame_length) {
        return {{}, CodecError::invalid_length};
    }

    auto encoded_header = encode_header(
        {static_cast<std::uint32_t>(total_size), version, type});
    if (!encoded_header) {
        return encoded_header;
    }
    auto& bytes = encoded_header.value;
    bytes.reserve(total_size);
    append_u16(bytes, static_cast<std::uint16_t>(queue_path.size()));
    bytes.insert(bytes.end(), queue_path.begin(), queue_path.end());
    append_u16(bytes, static_cast<std::uint16_t>(buffer_path.size()));
    bytes.insert(bytes.end(), buffer_path.begin(), buffer_path.end());
    return encoded_header;
}

MetadataResult decode_shared_memory_metadata(const std::uint8_t* frame,
                                             std::size_t size,
                                             std::uint32_t max_frame_length) {
    const auto decoded_header = decode_header(frame, size, max_frame_length);
    if (!decoded_header) {
        return {{}, decoded_header.error};
    }
    const auto frame_error = validate_complete_frame(decoded_header.value, size);
    if (frame_error != CodecError::none) {
        return {{}, frame_error};
    }
    if (!is_metadata_event(decoded_header.value.type)) {
        return {{}, CodecError::invalid_event_for_payload};
    }

    std::size_t offset = header_size;
    if (size - offset < 2U) {
        return {{}, CodecError::truncated_body};
    }
    const auto queue_length = static_cast<std::size_t>(read_u16(frame + offset));
    offset += 2U;
    if (size - offset < queue_length) {
        return {{}, CodecError::truncated_body};
    }
    const std::string queue_path(reinterpret_cast<const char*>(frame + offset),
                                 queue_length);
    offset += queue_length;
    if (size - offset < 2U) {
        return {{}, CodecError::truncated_body};
    }
    const auto buffer_length = static_cast<std::size_t>(read_u16(frame + offset));
    offset += 2U;
    if (size - offset < buffer_length) {
        return {{}, CodecError::truncated_body};
    }
    const std::string buffer_path(reinterpret_cast<const char*>(frame + offset),
                                  buffer_length);
    offset += buffer_length;
    if (offset != size) {
        return {{}, CodecError::trailing_bytes};
    }

    return {{decoded_header.value, queue_path, buffer_path}, CodecError::none};
}

BytesResult encode_fallback_data(std::uint8_t version, std::uint32_t stream_id,
                                 std::uint32_t status,
                                 const std::vector<std::uint8_t>& payload) {
    if (payload.size() > default_max_frame_length - fallback_prefix_size) {
        return {{}, CodecError::invalid_length};
    }
    const auto total_size = fallback_prefix_size + payload.size();
    auto encoded_header = encode_header({static_cast<std::uint32_t>(total_size),
                                         version, EventType::fallback_data});
    if (!encoded_header) {
        return encoded_header;
    }
    auto& bytes = encoded_header.value;
    bytes.reserve(total_size);
    append_u32(bytes, stream_id);
    append_u32(bytes, status);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return encoded_header;
}

FallbackResult decode_fallback_data(const std::uint8_t* frame, std::size_t size,
                                    std::uint32_t max_frame_length) {
    const auto decoded_header = decode_header(frame, size, max_frame_length);
    if (!decoded_header) {
        return {{}, decoded_header.error};
    }
    const auto frame_error = validate_complete_frame(decoded_header.value, size);
    if (frame_error != CodecError::none) {
        return {{}, frame_error};
    }
    if (decoded_header.value.type != EventType::fallback_data) {
        return {{}, CodecError::invalid_event_for_payload};
    }
    if (size < fallback_prefix_size) {
        return {{}, CodecError::truncated_body};
    }

    FallbackData fallback;
    fallback.header = decoded_header.value;
    fallback.stream_id = read_u32(frame + header_size);
    fallback.raw_status = read_u32(frame + header_size + 4U);
    fallback.stream_state = static_cast<std::uint8_t>(fallback.raw_status & 0xffU);
    fallback.payload.assign(frame + fallback_prefix_size, frame + size);
    return {std::move(fallback), CodecError::none};
}

}  // namespace shmipc::protocol
