#include "shm/queue_layout.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using shmipc::shm::LayoutError;
using shmipc::shm::QueueArchitecture;

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
    if (encoded.empty() || encoded.size() % 2U != 0U) {
        return {};
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        const auto high = hex_digit(encoded[index]);
        const auto low = hex_digit(encoded[index + 1U]);
        if (high < 0 || low < 0) {
            return {};
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return bytes;
}

bool parse_hex_u64(const std::string& encoded, std::uint64_t& value) {
    std::istringstream input(encoded);
    input >> std::hex >> value;
    return input && input.eof();
}

bool test_golden() {
    std::ifstream fixture(SHMIPC_QUEUE_LAYOUT_GOLDEN_PATH);
    std::string line;
    std::size_t rows = 0;
    while (std::getline(fixture, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::string architecture_name;
        std::uint32_t capacity = 0;
        std::string head_hex;
        std::string tail_hex;
        std::string working_hex;
        std::string sequence_hex;
        std::string buffer_offset_hex;
        std::string status_hex;
        std::string bytes_hex;
        std::string trailing;
        std::istringstream fields(line);
        if (!(fields >> architecture_name >> capacity >> head_hex >> tail_hex >>
              working_hex >> sequence_hex >> buffer_offset_hex >> status_hex >>
              bytes_hex) ||
            (fields >> trailing)) {
            return false;
        }

        std::uint64_t head = 0;
        std::uint64_t tail = 0;
        std::uint64_t working = 0;
        std::uint64_t sequence = 0;
        std::uint64_t buffer_offset = 0;
        std::uint64_t status = 0;
        if (!parse_hex_u64(head_hex, head) || !parse_hex_u64(tail_hex, tail) ||
            !parse_hex_u64(working_hex, working) ||
            !parse_hex_u64(sequence_hex, sequence) ||
            !parse_hex_u64(buffer_offset_hex, buffer_offset) ||
            !parse_hex_u64(status_hex, status)) {
            return false;
        }
        const auto architecture = architecture_name == "amd64"
                                      ? QueueArchitecture::amd64
                                      : QueueArchitecture::arm64;
        if (std::string(shmipc::shm::to_string(architecture)) != architecture_name) {
            return false;
        }
        auto expected = decode_hex(bytes_hex);
        const auto expected_size = shmipc::shm::queue_region_size(capacity);
        if (!expected_size || expected.size() != expected_size.value) {
            return false;
        }
        std::vector<std::uint8_t> actual(expected.size(), 0);
        const shmipc::shm::QueueHeader header{
            capacity, static_cast<std::int64_t>(head),
            static_cast<std::int64_t>(tail), static_cast<std::uint32_t>(working)};
        const shmipc::shm::QueueElement element{
            static_cast<std::uint32_t>(sequence),
            static_cast<std::uint32_t>(buffer_offset),
            static_cast<std::uint32_t>(status)};
        if (shmipc::shm::write_queue_header(actual.data(), actual.size(),
                                            architecture, header) !=
                LayoutError::none ||
            shmipc::shm::write_queue_element(actual.data(), actual.size(), 0,
                                             element) !=
                LayoutError::none ||
            actual != expected) {
            return false;
        }
        const auto decoded_header = shmipc::shm::read_queue_header(
            actual.data(), actual.size(), architecture);
        const auto decoded_element =
            shmipc::shm::read_queue_element(actual.data(), actual.size(), 0);
        if (!decoded_header || !decoded_element ||
            decoded_header.value.capacity != header.capacity ||
            decoded_header.value.head != header.head ||
            decoded_header.value.tail != header.tail ||
            decoded_header.value.working != header.working ||
            decoded_element.value.sequence_id != element.sequence_id ||
            decoded_element.value.buffer_offset != element.buffer_offset ||
            decoded_element.value.status != element.status) {
            return false;
        }
        ++rows;
    }
    return rows == 2U;
}

bool test_offsets_and_errors() {
    const auto amd64 = shmipc::shm::queue_offsets(QueueArchitecture::amd64);
    const auto arm64 = shmipc::shm::queue_offsets(QueueArchitecture::arm64);
    if (amd64.capacity != 0U || amd64.head != 4U || amd64.tail != 12U ||
        amd64.working != 20U || amd64.elements != 24U ||
        arm64.capacity != 0U || arm64.working != 4U || arm64.head != 8U ||
        arm64.tail != 16U || arm64.elements != 24U) {
        return false;
    }
    if (shmipc::shm::queue_region_size(0).error != LayoutError::zero_capacity ||
        shmipc::shm::queue_manager_region_size(1, QueueArchitecture::arm64)
                .error != LayoutError::invalid_manager_alignment ||
        !shmipc::shm::queue_manager_region_size(1, QueueArchitecture::amd64) ||
        !shmipc::shm::queue_manager_region_size(2, QueueArchitecture::arm64)) {
        return false;
    }

    std::vector<std::uint8_t> short_header(23U, 0);
    if (shmipc::shm::read_queue_header(nullptr, 0, QueueArchitecture::amd64)
            .error != LayoutError::null_memory ||
        shmipc::shm::read_queue_header(short_header.data(), short_header.size(),
                                      QueueArchitecture::amd64)
                .error != LayoutError::truncated_header) {
        return false;
    }
    std::vector<std::uint8_t> memory(36U, 0);
    const shmipc::shm::QueueHeader header{2, 0, 0, 0};
    if (shmipc::shm::write_queue_header(memory.data(), memory.size(),
                                        QueueArchitecture::amd64, header) !=
        LayoutError::truncated_elements) {
        return false;
    }
    memory.resize(48U);
    if (shmipc::shm::write_queue_header(memory.data(), memory.size(),
                                        QueueArchitecture::amd64, header) !=
            LayoutError::none ||
        shmipc::shm::read_queue_element(memory.data(), memory.size(), 2)
                .error != LayoutError::slot_out_of_range) {
        return false;
    }
    return true;
}

}  // namespace

int main() {
    if (!test_golden()) {
        std::cerr << "queue layout golden test failed\n";
        return 1;
    }
    if (!test_offsets_and_errors()) {
        std::cerr << "queue layout error-path test failed\n";
        return 1;
    }
    return 0;
}
