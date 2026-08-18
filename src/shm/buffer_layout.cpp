#include "shm/buffer_layout.hpp"

#include <cstring>
#include <limits>

namespace shmipc::shm {
namespace {

constexpr std::size_t manager_list_count_offset = 0;
constexpr std::size_t manager_used_length_offset = 4;
constexpr std::size_t list_size_offset = 0;
constexpr std::size_t list_capacity_offset = 4;
constexpr std::size_t list_head_offset = 8;
constexpr std::size_t list_tail_offset = 12;
constexpr std::size_t list_capacity_per_buffer_offset = 16;
constexpr std::size_t creator_counter_offset = 20;
constexpr std::size_t mapper_counter_offset = 24;
constexpr std::size_t slice_capacity_offset = 0;
constexpr std::size_t slice_size_offset = 4;
constexpr std::size_t slice_data_start_offset = 8;
constexpr std::size_t slice_next_offset = 12;
constexpr std::size_t slice_flags_offset = 16;

template <typename T>
T load_native(const std::uint8_t* memory, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, memory + offset, sizeof(value));
    return value;
}

template <typename T>
void store_native(std::uint8_t* memory, std::size_t offset, T value) noexcept {
    std::memcpy(memory + offset, &value, sizeof(value));
}

BufferLayoutError validate_memory(const std::uint8_t* memory, std::size_t size,
                                  std::size_t required) noexcept {
    if (memory == nullptr) {
        return BufferLayoutError::null_memory;
    }
    if (size < required) {
        return BufferLayoutError::truncated_header;
    }
    return BufferLayoutError::none;
}

}  // namespace

const char* to_string(BufferListRole role) noexcept {
    switch (role) {
        case BufferListRole::creator:
            return "creator";
        case BufferListRole::mapper:
            return "mapper";
    }
    return "unknown buffer-list role";
}

const char* to_string(BufferLayoutError error) noexcept {
    switch (error) {
        case BufferLayoutError::none:
            return "none";
        case BufferLayoutError::null_memory:
            return "null memory";
        case BufferLayoutError::truncated_header:
            return "truncated header";
        case BufferLayoutError::invalid_field:
            return "invalid field";
        case BufferLayoutError::size_overflow:
            return "size overflow";
        case BufferLayoutError::truncated_region:
            return "truncated region";
    }
    return "unknown buffer layout error";
}

std::size_t buffer_list_counter_offset(BufferListRole role) noexcept {
    return role == BufferListRole::creator ? creator_counter_offset
                                           : mapper_counter_offset;
}

BufferSizeResult buffer_list_region_size(std::uint32_t capacity,
                                         std::uint32_t capacity_per_buffer) noexcept {
    if (capacity == 0 || capacity_per_buffer == 0) {
        return {0, BufferLayoutError::invalid_field};
    }
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(capacity_per_buffer) >
        maximum - buffer_slice_header_size) {
        return {0, BufferLayoutError::size_overflow};
    }
    const auto stride = static_cast<std::size_t>(capacity_per_buffer) +
                        buffer_slice_header_size;
    if (static_cast<std::size_t>(capacity) >
        (maximum - buffer_list_header_size) / stride) {
        return {0, BufferLayoutError::size_overflow};
    }
    return {buffer_list_header_size + static_cast<std::size_t>(capacity) * stride,
            BufferLayoutError::none};
}

BufferLayoutError write_buffer_manager_header(
    std::uint8_t* memory, std::size_t size,
    const BufferManagerHeader& header) noexcept {
    const auto error = validate_memory(memory, size, buffer_manager_header_size);
    if (error != BufferLayoutError::none) {
        return error;
    }
    if (header.list_count == 0 || header.used_length == 0) {
        return BufferLayoutError::invalid_field;
    }
    store_native(memory, manager_list_count_offset, header.list_count);
    store_native(memory, manager_used_length_offset, header.used_length);
    return BufferLayoutError::none;
}

BufferManagerHeaderResult read_buffer_manager_header(const std::uint8_t* memory,
                                                     std::size_t size) noexcept {
    const auto error = validate_memory(memory, size, buffer_manager_header_size);
    if (error != BufferLayoutError::none) {
        return {{}, error};
    }
    const BufferManagerHeader header{
        load_native<std::uint16_t>(memory, manager_list_count_offset),
        load_native<std::uint32_t>(memory, manager_used_length_offset)};
    if (header.list_count == 0 || header.used_length == 0) {
        return {{}, BufferLayoutError::invalid_field};
    }
    if (header.used_length > size - buffer_manager_header_size) {
        return {{}, BufferLayoutError::truncated_region};
    }
    return {header, BufferLayoutError::none};
}

BufferLayoutError write_buffer_list_header(std::uint8_t* memory, std::size_t size,
                                           const BufferListHeader& header) noexcept {
    const auto error = validate_memory(memory, size, buffer_list_header_size);
    if (error != BufferLayoutError::none) {
        return error;
    }
    const auto expected_size =
        buffer_list_region_size(header.capacity, header.capacity_per_buffer);
    if (!expected_size) {
        return expected_size.error;
    }
    if (size < expected_size.value) {
        return BufferLayoutError::truncated_region;
    }
    if (header.size < 0 || static_cast<std::uint32_t>(header.size) > header.capacity) {
        return BufferLayoutError::invalid_field;
    }
    store_native(memory, list_size_offset, header.size);
    store_native(memory, list_capacity_offset, header.capacity);
    store_native(memory, list_head_offset, header.head);
    store_native(memory, list_tail_offset, header.tail);
    store_native(memory, list_capacity_per_buffer_offset,
                 header.capacity_per_buffer);
    store_native(memory, creator_counter_offset, header.creator_counter);
    store_native(memory, mapper_counter_offset, header.mapper_counter);
    return BufferLayoutError::none;
}

BufferListHeaderResult read_buffer_list_header(const std::uint8_t* memory,
                                               std::size_t size) noexcept {
    const auto error = validate_memory(memory, size, buffer_list_header_size);
    if (error != BufferLayoutError::none) {
        return {{}, error};
    }
    const BufferListHeader header{
        load_native<std::int32_t>(memory, list_size_offset),
        load_native<std::uint32_t>(memory, list_capacity_offset),
        load_native<std::uint32_t>(memory, list_head_offset),
        load_native<std::uint32_t>(memory, list_tail_offset),
        load_native<std::uint32_t>(memory, list_capacity_per_buffer_offset),
        load_native<std::int32_t>(memory, creator_counter_offset),
        load_native<std::int32_t>(memory, mapper_counter_offset)};
    const auto expected_size =
        buffer_list_region_size(header.capacity, header.capacity_per_buffer);
    if (!expected_size) {
        return {{}, expected_size.error};
    }
    if (size < expected_size.value) {
        return {{}, BufferLayoutError::truncated_region};
    }
    if (header.size < 0 || static_cast<std::uint32_t>(header.size) > header.capacity) {
        return {{}, BufferLayoutError::invalid_field};
    }
    return {header, BufferLayoutError::none};
}

BufferLayoutError write_buffer_slice_header(std::uint8_t* memory, std::size_t size,
                                            const BufferSliceHeader& header) noexcept {
    const auto error = validate_memory(memory, size, buffer_slice_header_size);
    if (error != BufferLayoutError::none) {
        return error;
    }
    if (header.data_start > header.capacity ||
        header.size > header.capacity - header.data_start) {
        return BufferLayoutError::invalid_field;
    }
    store_native(memory, slice_capacity_offset, header.capacity);
    store_native(memory, slice_size_offset, header.size);
    store_native(memory, slice_data_start_offset, header.data_start);
    store_native(memory, slice_next_offset, header.next_offset);
    store_native(memory, slice_flags_offset, header.flags);
    return BufferLayoutError::none;
}

BufferSliceHeaderResult read_buffer_slice_header(const std::uint8_t* memory,
                                                 std::size_t size) noexcept {
    const auto error = validate_memory(memory, size, buffer_slice_header_size);
    if (error != BufferLayoutError::none) {
        return {{}, error};
    }
    const BufferSliceHeader header{
        load_native<std::uint32_t>(memory, slice_capacity_offset),
        load_native<std::uint32_t>(memory, slice_size_offset),
        load_native<std::uint32_t>(memory, slice_data_start_offset),
        load_native<std::uint32_t>(memory, slice_next_offset),
        load_native<std::uint8_t>(memory, slice_flags_offset)};
    if (header.data_start > header.capacity ||
        header.size > header.capacity - header.data_start) {
        return {{}, BufferLayoutError::invalid_field};
    }
    return {header, BufferLayoutError::none};
}

}  // namespace shmipc::shm
