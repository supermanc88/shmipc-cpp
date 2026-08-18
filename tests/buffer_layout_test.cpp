#include "shm/buffer_layout.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

using shmipc::shm::BufferLayoutError;

struct LayoutRow {
    std::string kind;
    std::size_t total_size{};
    std::map<std::string, std::size_t> offsets;
};

bool parse_row(const std::string& line, LayoutRow& row) {
    std::istringstream fields(line);
    if (!(fields >> row.kind >> row.total_size)) {
        return false;
    }
    std::string field;
    while (fields >> field) {
        const auto separator = field.find('=');
        if (separator == std::string::npos) {
            return false;
        }
        std::istringstream value(field.substr(separator + 1U));
        std::size_t offset = 0;
        if (!(value >> offset) || !value.eof()) {
            return false;
        }
        row.offsets.emplace(field.substr(0, separator), offset);
    }
    return true;
}

template <typename T>
T read_native(const std::vector<std::uint8_t>& memory, std::size_t offset) {
    T value{};
    std::memcpy(&value, memory.data() + offset, sizeof(value));
    return value;
}

template <typename T>
void write_native(std::vector<std::uint8_t>& memory, std::size_t offset, T value) {
    std::memcpy(memory.data() + offset, &value, sizeof(value));
}

bool test_manager_row(const LayoutRow& row) {
    if (row.total_size != shmipc::shm::buffer_manager_header_size ||
        row.offsets.at("list_count") != 0U ||
        row.offsets.at("used_length") != 4U) {
        return false;
    }
    std::vector<std::uint8_t> memory(64U, 0);
    const shmipc::shm::BufferManagerHeader expected{3, 56};
    if (shmipc::shm::write_buffer_manager_header(memory.data(), memory.size(),
                                                 expected) !=
            BufferLayoutError::none ||
        read_native<std::uint16_t>(memory, row.offsets.at("list_count")) !=
            expected.list_count ||
        read_native<std::uint32_t>(memory, row.offsets.at("used_length")) !=
            expected.used_length) {
        return false;
    }
    const auto actual =
        shmipc::shm::read_buffer_manager_header(memory.data(), memory.size());
    return actual && actual.value.list_count == expected.list_count &&
           actual.value.used_length == expected.used_length;
}

bool test_list_row(const LayoutRow& row) {
    if (row.total_size != shmipc::shm::buffer_list_header_size ||
        row.offsets.at("region") != shmipc::shm::buffer_list_header_size ||
        row.offsets.at("creator_counter") !=
            shmipc::shm::buffer_list_counter_offset(
                shmipc::shm::BufferListRole::creator) ||
        row.offsets.at("mapper_counter") !=
            shmipc::shm::buffer_list_counter_offset(
                shmipc::shm::BufferListRole::mapper)) {
        return false;
    }
    const auto region_size = shmipc::shm::buffer_list_region_size(3, 4);
    if (!region_size) {
        return false;
    }
    std::vector<std::uint8_t> memory(region_size.value, 0);
    const shmipc::shm::BufferListHeader expected{2, 3, 24, 48, 4, 1, -1};
    if (shmipc::shm::write_buffer_list_header(memory.data(), memory.size(),
                                              expected) !=
        BufferLayoutError::none) {
        return false;
    }
    const auto actual =
        shmipc::shm::read_buffer_list_header(memory.data(), memory.size());
    return actual && actual.value.size == expected.size &&
           actual.value.capacity == expected.capacity &&
           actual.value.head == expected.head && actual.value.tail == expected.tail &&
           actual.value.capacity_per_buffer == expected.capacity_per_buffer &&
           actual.value.creator_counter == expected.creator_counter &&
           actual.value.mapper_counter == expected.mapper_counter &&
           read_native<std::int32_t>(memory, row.offsets.at("creator_counter")) == 1 &&
           read_native<std::int32_t>(memory, row.offsets.at("mapper_counter")) == -1;
}

bool test_slice_row(const LayoutRow& row) {
    if (row.total_size != shmipc::shm::buffer_slice_header_size) {
        return false;
    }
    std::vector<std::uint8_t> memory(row.total_size, 0);
    const shmipc::shm::BufferSliceHeader expected{16, 12, 3, 64, 3};
    if (shmipc::shm::write_buffer_slice_header(memory.data(), memory.size(),
                                               expected) !=
        BufferLayoutError::none) {
        return false;
    }
    const auto actual =
        shmipc::shm::read_buffer_slice_header(memory.data(), memory.size());
    return actual && actual.value.capacity == expected.capacity &&
           actual.value.size == expected.size &&
           actual.value.data_start == expected.data_start &&
           actual.value.next_offset == expected.next_offset &&
           actual.value.flags == expected.flags &&
           read_native<std::uint8_t>(memory, row.offsets.at("flags")) == 3U;
}

bool test_golden() {
    std::ifstream fixture(SHMIPC_BUFFER_LAYOUT_GOLDEN_PATH);
    std::string line;
    std::size_t rows = 0;
    while (std::getline(fixture, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        LayoutRow row;
        if (!parse_row(line, row)) {
            return false;
        }
        bool valid = false;
        if (row.kind == "manager") {
            valid = test_manager_row(row);
        } else if (row.kind == "list") {
            valid = test_list_row(row);
        } else if (row.kind == "slice") {
            valid = test_slice_row(row);
        }
        if (!valid) {
            return false;
        }
        ++rows;
    }
    return rows == 3U;
}

bool test_errors() {
    std::vector<std::uint8_t> short_memory(19U, 0);
    if (shmipc::shm::read_buffer_slice_header(nullptr, 0).error !=
            BufferLayoutError::null_memory ||
        shmipc::shm::read_buffer_slice_header(short_memory.data(),
                                             short_memory.size())
                .error != BufferLayoutError::truncated_header ||
        shmipc::shm::buffer_list_region_size(0, 4).error !=
            BufferLayoutError::invalid_field) {
        return false;
    }
    std::vector<std::uint8_t> list_memory(60U, 0);
    const shmipc::shm::BufferListHeader list{1, 2, 0, 24, 4, 0, 0};
    if (shmipc::shm::write_buffer_list_header(list_memory.data(),
                                              list_memory.size(), list) !=
        BufferLayoutError::truncated_region) {
        return false;
    }
    std::vector<std::uint8_t> slice_memory(20U, 0);
    return shmipc::shm::write_buffer_slice_header(
               slice_memory.data(), slice_memory.size(), {8, 9, 0, 0, 0}) ==
           BufferLayoutError::invalid_field;
}

std::vector<std::uint8_t> make_valid_chain() {
    const auto region_size = shmipc::shm::buffer_list_region_size(3, 4);
    if (!region_size) {
        return {};
    }
    std::vector<std::uint8_t> memory(region_size.value, 0);
    if (shmipc::shm::write_buffer_list_header(
            memory.data(), memory.size(), {3, 3, 0, 48, 4, 0, 0}) !=
        BufferLayoutError::none) {
        return {};
    }
    const std::size_t first = shmipc::shm::buffer_list_header_size;
    const std::size_t second = first + 24U;
    const std::size_t third = second + 24U;
    if (shmipc::shm::write_buffer_slice_header(
            memory.data() + first, memory.size() - first, {4, 0, 0, 24, 1}) !=
            BufferLayoutError::none ||
        shmipc::shm::write_buffer_slice_header(
            memory.data() + second, memory.size() - second, {4, 0, 0, 48, 1}) !=
            BufferLayoutError::none ||
        shmipc::shm::write_buffer_slice_header(
            memory.data() + third, memory.size() - third, {4, 0, 0, 0, 0}) !=
            BufferLayoutError::none) {
        return {};
    }
    return memory;
}

BufferLayoutError expected_error(const std::string& name) {
    if (name == "truncated_region") {
        return BufferLayoutError::truncated_region;
    }
    if (name == "invalid_offset") {
        return BufferLayoutError::invalid_offset;
    }
    if (name == "size_overflow") {
        return BufferLayoutError::size_overflow;
    }
    if (name == "cyclic_chain") {
        return BufferLayoutError::cyclic_chain;
    }
    if (name == "invalid_tail") {
        return BufferLayoutError::invalid_tail;
    }
    if (name == "invalid_slice_capacity") {
        return BufferLayoutError::invalid_slice_capacity;
    }
    return BufferLayoutError::invalid_field;
}

bool test_corruption_corpus() {
    std::ifstream corpus(SHMIPC_LAYOUT_CORRUPTION_CORPUS_PATH);
    std::string line;
    std::size_t rows = 0;
    while (std::getline(corpus, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::string mutation;
        std::string error_name;
        std::string trailing;
        std::istringstream fields(line);
        if (!(fields >> mutation >> error_name) || (fields >> trailing)) {
            return false;
        }
        if (mutation == "size_overflow") {
            if (shmipc::shm::buffer_list_region_size(0xffffffffU, 0xffffffffU)
                    .error != expected_error(error_name)) {
                return false;
            }
            ++rows;
            continue;
        }
        auto memory = make_valid_chain();
        if (memory.empty() ||
            shmipc::shm::validate_buffer_list_chain(memory.data(), memory.size()) !=
                BufferLayoutError::none) {
            return false;
        }
        if (mutation == "truncated_region") {
            memory.pop_back();
        } else if (mutation == "oversized_capacity") {
            write_native<std::uint32_t>(memory, 4U, 4U);
        } else if (mutation == "misaligned_head") {
            write_native<std::uint32_t>(memory, 8U, 1U);
        } else if (mutation == "out_of_range_next") {
            write_native<std::uint32_t>(memory, 48U, 72U);
        } else if (mutation == "cycle") {
            write_native<std::uint32_t>(memory, 72U, 0U);
        } else if (mutation == "tail_mismatch") {
            write_native<std::uint8_t>(memory, 76U, 0U);
        } else if (mutation == "slice_capacity") {
            write_native<std::uint32_t>(memory, 60U, 8U);
        } else if (mutation == "invalid_data_range") {
            write_native<std::uint32_t>(memory, 64U, 5U);
        } else {
            return false;
        }
        if (shmipc::shm::validate_buffer_list_chain(memory.data(), memory.size()) !=
            expected_error(error_name)) {
            return false;
        }
        ++rows;
    }
    return rows == 9U;
}

}  // namespace

int main() {
    if (!test_golden()) {
        std::cerr << "buffer layout golden test failed\n";
        return 1;
    }
    if (!test_errors()) {
        std::cerr << "buffer layout error-path test failed\n";
        return 1;
    }
    if (!test_corruption_corpus()) {
        std::cerr << "buffer layout corruption corpus failed\n";
        return 1;
    }
    return 0;
}
