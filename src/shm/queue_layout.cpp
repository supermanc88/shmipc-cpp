#include "shm/queue_layout.hpp"

#include <cstring>
#include <limits>

namespace shmipc::shm {
namespace {

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

LayoutError validate_region(const std::uint8_t* memory, std::size_t size,
                            std::uint32_t& capacity) noexcept {
    if (memory == nullptr) {
        return LayoutError::null_memory;
    }
    if (size < queue_header_size) {
        return LayoutError::truncated_header;
    }
    capacity = load_native<std::uint32_t>(memory, 0);
    if (capacity == 0) {
        return LayoutError::zero_capacity;
    }
    const auto expected_size = queue_region_size(capacity);
    if (!expected_size) {
        return expected_size.error;
    }
    if (size < expected_size.value) {
        return LayoutError::truncated_elements;
    }
    return LayoutError::none;
}

std::size_t element_offset(std::uint32_t slot) noexcept {
    return queue_header_size + static_cast<std::size_t>(slot) * queue_element_size;
}

}  // namespace

const char* to_string(QueueArchitecture architecture) noexcept {
    switch (architecture) {
        case QueueArchitecture::amd64:
            return "amd64";
        case QueueArchitecture::arm64:
            return "arm64";
    }
    return "unknown queue architecture";
}

const char* to_string(LayoutError error) noexcept {
    switch (error) {
        case LayoutError::none:
            return "none";
        case LayoutError::null_memory:
            return "null memory";
        case LayoutError::truncated_header:
            return "truncated header";
        case LayoutError::zero_capacity:
            return "zero capacity";
        case LayoutError::size_overflow:
            return "size overflow";
        case LayoutError::truncated_elements:
            return "truncated elements";
        case LayoutError::slot_out_of_range:
            return "slot out of range";
        case LayoutError::invalid_manager_alignment:
            return "invalid manager alignment";
    }
    return "unknown layout error";
}

QueueOffsets queue_offsets(QueueArchitecture architecture) noexcept {
    if (architecture == QueueArchitecture::arm64) {
        return {0, 8, 16, 4, queue_header_size};
    }
    return {0, 4, 12, 20, queue_header_size};
}

SizeResult queue_region_size(std::uint32_t capacity) noexcept {
    if (capacity == 0) {
        return {0, LayoutError::zero_capacity};
    }
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (static_cast<std::size_t>(capacity) >
        (maximum - queue_header_size) / queue_element_size) {
        return {0, LayoutError::size_overflow};
    }
    return {queue_header_size + static_cast<std::size_t>(capacity) *
                                    queue_element_size,
            LayoutError::none};
}

SizeResult queue_manager_region_size(std::uint32_t capacity,
                                     QueueArchitecture architecture) noexcept {
    const auto queue_size = queue_region_size(capacity);
    if (!queue_size || queue_size.value >
                           std::numeric_limits<std::size_t>::max() / queue_count) {
        return {0, queue_size ? LayoutError::size_overflow : queue_size.error};
    }
    const auto manager_size = queue_size.value * queue_count;
    if (architecture == QueueArchitecture::arm64 && manager_size % 16U != 0U) {
        return {0, LayoutError::invalid_manager_alignment};
    }
    return {manager_size, LayoutError::none};
}

LayoutError write_queue_header(std::uint8_t* memory, std::size_t size,
                               QueueArchitecture architecture,
                               const QueueHeader& header) noexcept {
    if (memory == nullptr) {
        return LayoutError::null_memory;
    }
    if (size < queue_header_size) {
        return LayoutError::truncated_header;
    }
    const auto expected_size = queue_region_size(header.capacity);
    if (!expected_size) {
        return expected_size.error;
    }
    if (size < expected_size.value) {
        return LayoutError::truncated_elements;
    }

    const auto offsets = queue_offsets(architecture);
    store_native(memory, offsets.capacity, header.capacity);
    store_native(memory, offsets.head, header.head);
    store_native(memory, offsets.tail, header.tail);
    store_native(memory, offsets.working, header.working);
    return LayoutError::none;
}

HeaderResult read_queue_header(const std::uint8_t* memory, std::size_t size,
                               QueueArchitecture architecture) noexcept {
    std::uint32_t capacity = 0;
    const auto error = validate_region(memory, size, capacity);
    if (error != LayoutError::none) {
        return {{}, error};
    }
    const auto offsets = queue_offsets(architecture);
    return {{capacity, load_native<std::int64_t>(memory, offsets.head),
             load_native<std::int64_t>(memory, offsets.tail),
             load_native<std::uint32_t>(memory, offsets.working)},
            LayoutError::none};
}

LayoutError write_queue_element(std::uint8_t* memory, std::size_t size,
                                std::uint32_t slot,
                                const QueueElement& element) noexcept {
    std::uint32_t capacity = 0;
    const auto error = validate_region(memory, size, capacity);
    if (error != LayoutError::none) {
        return error;
    }
    if (slot >= capacity) {
        return LayoutError::slot_out_of_range;
    }
    const auto offset = element_offset(slot);
    store_native(memory, offset, element.sequence_id);
    store_native(memory, offset + 4U, element.buffer_offset);
    store_native(memory, offset + 8U, element.status);
    return LayoutError::none;
}

ElementResult read_queue_element(const std::uint8_t* memory, std::size_t size,
                                 std::uint32_t slot) noexcept {
    std::uint32_t capacity = 0;
    const auto error = validate_region(memory, size, capacity);
    if (error != LayoutError::none) {
        return {{}, error};
    }
    if (slot >= capacity) {
        return {{}, LayoutError::slot_out_of_range};
    }
    const auto offset = element_offset(slot);
    return {{load_native<std::uint32_t>(memory, offset),
             load_native<std::uint32_t>(memory, offset + 4U),
             load_native<std::uint32_t>(memory, offset + 8U)},
            LayoutError::none};
}

}  // namespace shmipc::shm
