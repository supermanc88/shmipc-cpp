#pragma once

#include <cstddef>
#include <cstdint>

namespace shmipc::shm {

constexpr std::size_t buffer_manager_header_size = 8;
constexpr std::size_t buffer_list_header_size = 36;
constexpr std::size_t buffer_slice_header_size = 20;

enum class BufferListRole {
    creator,
    mapper,
};

enum class BufferLayoutError {
    none,
    null_memory,
    truncated_header,
    invalid_field,
    size_overflow,
    truncated_region,
};

struct BufferManagerHeader {
    std::uint16_t list_count;
    std::uint32_t used_length;
};

struct BufferListHeader {
    std::int32_t size;
    std::uint32_t capacity;
    std::uint32_t head;
    std::uint32_t tail;
    std::uint32_t capacity_per_buffer;
    std::int32_t creator_counter;
    std::int32_t mapper_counter;
};

struct BufferSliceHeader {
    std::uint32_t capacity;
    std::uint32_t size;
    std::uint32_t data_start;
    std::uint32_t next_offset;
    std::uint8_t flags;
};

template <typename T>
struct BufferLayoutResult {
    T value{};
    BufferLayoutError error{BufferLayoutError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == BufferLayoutError::none;
    }
};

using BufferSizeResult = BufferLayoutResult<std::size_t>;
using BufferManagerHeaderResult = BufferLayoutResult<BufferManagerHeader>;
using BufferListHeaderResult = BufferLayoutResult<BufferListHeader>;
using BufferSliceHeaderResult = BufferLayoutResult<BufferSliceHeader>;

[[nodiscard]] const char* to_string(BufferListRole role) noexcept;
[[nodiscard]] const char* to_string(BufferLayoutError error) noexcept;
[[nodiscard]] std::size_t buffer_list_counter_offset(BufferListRole role) noexcept;

[[nodiscard]] BufferSizeResult buffer_list_region_size(
    std::uint32_t capacity, std::uint32_t capacity_per_buffer) noexcept;

[[nodiscard]] BufferLayoutError write_buffer_manager_header(
    std::uint8_t* memory, std::size_t size,
    const BufferManagerHeader& header) noexcept;
[[nodiscard]] BufferManagerHeaderResult read_buffer_manager_header(
    const std::uint8_t* memory, std::size_t size) noexcept;

[[nodiscard]] BufferLayoutError write_buffer_list_header(
    std::uint8_t* memory, std::size_t size,
    const BufferListHeader& header) noexcept;
[[nodiscard]] BufferListHeaderResult read_buffer_list_header(
    const std::uint8_t* memory, std::size_t size) noexcept;

[[nodiscard]] BufferLayoutError write_buffer_slice_header(
    std::uint8_t* memory, std::size_t size,
    const BufferSliceHeader& header) noexcept;
[[nodiscard]] BufferSliceHeaderResult read_buffer_slice_header(
    const std::uint8_t* memory, std::size_t size) noexcept;

}  // namespace shmipc::shm
