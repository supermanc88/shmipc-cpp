#include "protocol/control_codec.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using shmipc::protocol::CodecError;
using shmipc::protocol::EventType;

int hex_digit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

std::vector<std::uint8_t> decode_hex(const std::string& encoded) {
    if ((encoded.size() % 2U) != 0U) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        const int high = hex_digit(encoded[index]);
        const int low = hex_digit(encoded[index + 1U]);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

std::string bytes_to_string(const std::vector<std::uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool test_metadata_golden() {
    std::ifstream fixture(SHMIPC_SHM_METADATA_GOLDEN_PATH);
    std::string line;
    std::size_t rows = 0;
    while (std::getline(fixture, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::string event_name;
        unsigned int version = 0;
        std::string queue_hex;
        std::string buffer_hex;
        std::string frame_hex;
        std::istringstream fields(line);
        if (!(fields >> event_name >> version >> queue_hex >> buffer_hex >> frame_hex)) {
            return false;
        }
        const auto frame = decode_hex(frame_hex);
        const auto decoded = shmipc::protocol::decode_shared_memory_metadata(
            frame.data(), frame.size());
        if (!decoded || decoded.value.header.version != version ||
            shmipc::protocol::to_string(decoded.value.header.type) != event_name ||
            decoded.value.queue_path != bytes_to_string(decode_hex(queue_hex)) ||
            decoded.value.buffer_path != bytes_to_string(decode_hex(buffer_hex))) {
            return false;
        }
        const auto encoded = shmipc::protocol::encode_shared_memory_metadata(
            static_cast<std::uint8_t>(version), decoded.value.header.type,
            decoded.value.queue_path, decoded.value.buffer_path);
        if (!encoded || encoded.value != frame) {
            return false;
        }
        ++rows;
    }
    return rows == 2U;
}

bool test_fallback_golden() {
    std::ifstream fixture(SHMIPC_FALLBACK_GOLDEN_PATH);
    std::string line;
    while (std::getline(fixture, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        unsigned int version = 0;
        std::uint32_t stream_id = 0;
        std::uint32_t status = 0;
        std::string payload_hex;
        std::string frame_hex;
        std::istringstream fields(line);
        if (!(fields >> version >> stream_id >> status >> payload_hex >> frame_hex)) {
            return false;
        }
        const auto payload = decode_hex(payload_hex);
        const auto frame = decode_hex(frame_hex);
        const auto decoded =
            shmipc::protocol::decode_fallback_data(frame.data(), frame.size());
        if (!decoded || decoded.value.header.version != version ||
            decoded.value.stream_id != stream_id || decoded.value.raw_status != status ||
            decoded.value.stream_state != static_cast<std::uint8_t>(status & 0xffU) ||
            decoded.value.payload != payload) {
            return false;
        }
        const auto encoded = shmipc::protocol::encode_fallback_data(
            static_cast<std::uint8_t>(version), stream_id, status, payload);
        return encoded && encoded.value == frame;
    }
    return false;
}

bool test_invalid_inputs() {
    using shmipc::protocol::decode_fallback_data;
    using shmipc::protocol::decode_header;
    using shmipc::protocol::decode_shared_memory_metadata;
    using shmipc::protocol::encode_fallback_data;
    using shmipc::protocol::encode_header;
    using shmipc::protocol::encode_shared_memory_metadata;

    const std::vector<std::uint8_t> valid_header{0, 0, 0, 8, 0x77, 0x58, 3, 1};
    if (decode_header(valid_header.data(), 7).error != CodecError::truncated_header) {
        return false;
    }
    auto invalid = valid_header;
    invalid[3] = 7;
    if (decode_header(invalid.data(), invalid.size()).error != CodecError::invalid_length) {
        return false;
    }
    invalid = valid_header;
    invalid[4] = 0;
    if (decode_header(invalid.data(), invalid.size()).error != CodecError::invalid_magic) {
        return false;
    }
    invalid = valid_header;
    invalid[6] = 0;
    if (decode_header(invalid.data(), invalid.size()).error != CodecError::invalid_version) {
        return false;
    }
    invalid = valid_header;
    invalid[7] = 10;
    if (decode_header(invalid.data(), invalid.size()).error !=
        CodecError::invalid_event_type) {
        return false;
    }
    if (decode_header(valid_header.data(), valid_header.size(), 7U).error !=
        CodecError::invalid_length) {
        return false;
    }
    if (encode_header({8, 0, EventType::polling}).error !=
            CodecError::invalid_version ||
        encode_header({8, 3, static_cast<EventType>(10)}).error !=
            CodecError::invalid_event_type) {
        return false;
    }
    if (encode_shared_memory_metadata(3, EventType::polling, "queue", "buffer")
            .error != CodecError::invalid_event_for_payload ||
        encode_shared_memory_metadata(3, EventType::share_memory_by_memfd,
                                      std::string(65536U, 'q'), "buffer")
                .error != CodecError::field_too_long) {
        return false;
    }

    auto metadata = decode_hex(
        "0000001d77580200000871756575652d763200096275666665722d7632");
    if (decode_shared_memory_metadata(metadata.data(), metadata.size() - 1U).error !=
        CodecError::truncated_body) {
        return false;
    }
    metadata.push_back(0);
    if (decode_shared_memory_metadata(metadata.data(), metadata.size()).error !=
        CodecError::trailing_bytes) {
        return false;
    }
    metadata = decode_hex("0000000a775802000005");
    if (decode_shared_memory_metadata(metadata.data(), metadata.size()).error !=
        CodecError::truncated_body) {
        return false;
    }

    auto fallback = decode_hex("000000107758030310203040a1b2c3d4");
    fallback[3] = 15;
    fallback.resize(15U);
    if (decode_fallback_data(fallback.data(), fallback.size()).error !=
        CodecError::truncated_body) {
        return false;
    }
    fallback = valid_header;
    if (decode_fallback_data(fallback.data(), fallback.size()).error !=
        CodecError::invalid_event_for_payload) {
        return false;
    }
    if (encode_fallback_data(0, 1, 2, {}).error != CodecError::invalid_version) {
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!test_metadata_golden()) {
        std::cerr << "shared-memory metadata golden test failed\n";
        return 1;
    }
    if (!test_fallback_golden()) {
        std::cerr << "fallback golden test failed\n";
        return 1;
    }
    if (!test_invalid_inputs()) {
        std::cerr << "invalid-input protocol test failed\n";
        return 1;
    }
    return 0;
}
