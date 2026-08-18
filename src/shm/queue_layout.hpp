#pragma once

#include <cstddef>
#include <cstdint>

namespace shmipc::shm {

constexpr std::size_t queue_header_size = 24;
constexpr std::size_t queue_element_size = 12;
constexpr std::size_t queue_count = 2;

enum class QueueArchitecture {
    amd64,
    arm64,
};

enum class LayoutError {
    none,
    null_memory,
    truncated_header,
    zero_capacity,
    size_overflow,
    truncated_elements,
    slot_out_of_range,
    invalid_manager_alignment,
};

struct QueueOffsets {
    std::size_t capacity;
    std::size_t head;
    std::size_t tail;
    std::size_t working;
    std::size_t elements;
};

struct QueueHeader {
    std::uint32_t capacity;
    std::int64_t head;
    std::int64_t tail;
    std::uint32_t working;
};

struct QueueElement {
    std::uint32_t sequence_id;
    std::uint32_t buffer_offset;
    std::uint32_t status;
};

template <typename T>
struct LayoutResult {
    T value{};
    LayoutError error{LayoutError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == LayoutError::none;
    }
};

using SizeResult = LayoutResult<std::size_t>;
using HeaderResult = LayoutResult<QueueHeader>;
using ElementResult = LayoutResult<QueueElement>;

[[nodiscard]] const char* to_string(QueueArchitecture architecture) noexcept;
[[nodiscard]] const char* to_string(LayoutError error) noexcept;
[[nodiscard]] QueueOffsets queue_offsets(QueueArchitecture architecture) noexcept;

[[nodiscard]] SizeResult queue_region_size(std::uint32_t capacity) noexcept;
[[nodiscard]] SizeResult queue_manager_region_size(
    std::uint32_t capacity, QueueArchitecture architecture) noexcept;

[[nodiscard]] LayoutError write_queue_header(
    std::uint8_t* memory, std::size_t size, QueueArchitecture architecture,
    const QueueHeader& header) noexcept;
[[nodiscard]] HeaderResult read_queue_header(
    const std::uint8_t* memory, std::size_t size,
    QueueArchitecture architecture) noexcept;

[[nodiscard]] LayoutError write_queue_element(
    std::uint8_t* memory, std::size_t size, std::uint32_t slot,
    const QueueElement& element) noexcept;
[[nodiscard]] ElementResult read_queue_element(
    const std::uint8_t* memory, std::size_t size, std::uint32_t slot) noexcept;

}  // namespace shmipc::shm
