#include "shm/buffer_pool.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace shmipc::shm {
namespace {

constexpr std::uint8_t has_next_flag = 1U;
constexpr std::uint8_t in_use_flag = 2U;

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

BufferPoolError translate_layout_error(BufferLayoutError error) noexcept {
    switch (error) {
        case BufferLayoutError::none:
            return BufferPoolError::none;
        case BufferLayoutError::null_memory:
            return BufferPoolError::null_memory;
        case BufferLayoutError::size_overflow:
            return BufferPoolError::size_overflow;
        case BufferLayoutError::truncated_header:
        case BufferLayoutError::truncated_region:
            return BufferPoolError::truncated_region;
        case BufferLayoutError::invalid_field:
        case BufferLayoutError::invalid_offset:
        case BufferLayoutError::cyclic_chain:
        case BufferLayoutError::invalid_tail:
        case BufferLayoutError::invalid_slice_capacity:
            return BufferPoolError::invalid_layout;
    }
    return BufferPoolError::invalid_layout;
}

std::int32_t role_counter(const std::uint8_t* list,
                          BufferListRole role) noexcept {
    return load_native<std::int32_t>(list, buffer_list_counter_offset(role));
}

void set_role_counter(std::uint8_t* list, BufferListRole role,
                      std::int32_t value) noexcept {
    store_native(list, buffer_list_counter_offset(role), value);
}

bool valid_tiers(const std::vector<BufferTierSpec>& tiers) noexcept {
    if (tiers.empty() || tiers.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    std::uint32_t percent_sum = 0;
    std::uint32_t previous_capacity = 0;
    for (const auto& tier : tiers) {
        if (tier.capacity == 0 || tier.percent == 0 || tier.percent > 100U ||
            tier.capacity == previous_capacity || percent_sum > 100U - tier.percent) {
            return false;
        }
        percent_sum += tier.percent;
        previous_capacity = tier.capacity;
    }
    return percent_sum == 100U;
}

}  // namespace

BufferAllocation::BufferAllocation(std::uint8_t* owner, std::uint8_t* data,
                                   std::uint32_t capacity,
                                   std::uint32_t offset,
                                   std::size_t list_index,
                                   BufferListRole role) noexcept
    : owner_(owner),
      data_(data),
      capacity_(capacity),
      offset_(offset),
      list_index_(list_index),
      role_(role) {}

BufferAllocation::BufferAllocation(BufferAllocation&& other) noexcept
    : owner_(other.owner_),
      data_(other.data_),
      capacity_(other.capacity_),
      offset_(other.offset_),
      list_index_(other.list_index_),
      role_(other.role_) {
    other.invalidate();
}

BufferAllocation::operator bool() const noexcept { return owner_ != nullptr; }

std::uint8_t* BufferAllocation::data() noexcept { return data_; }

const std::uint8_t* BufferAllocation::data() const noexcept { return data_; }

std::uint32_t BufferAllocation::capacity() const noexcept { return capacity_; }

std::uint32_t BufferAllocation::offset() const noexcept { return offset_; }

void BufferAllocation::invalidate() noexcept {
    owner_ = nullptr;
    data_ = nullptr;
    capacity_ = 0;
    offset_ = 0;
    list_index_ = 0;
    role_ = BufferListRole::creator;
}

BufferPool::BufferPool(std::uint8_t* memory, std::size_t size,
                       std::size_t used_size, BufferListRole role,
                       std::vector<ListView> lists) noexcept
    : memory_(memory),
      size_(size),
      used_size_(used_size),
      role_(role),
      lists_(std::move(lists)) {}

BufferPool::operator bool() const noexcept {
    return memory_ != nullptr && !lists_.empty();
}

std::size_t BufferPool::list_count() const noexcept { return lists_.size(); }

std::uint32_t BufferPool::min_slice_capacity() const noexcept {
    return lists_.empty() ? 0U : lists_.front().capacity_per_buffer;
}

std::uint32_t BufferPool::max_slice_capacity() const noexcept {
    return lists_.empty() ? 0U : lists_.back().capacity_per_buffer;
}

std::size_t BufferPool::used_size() const noexcept { return used_size_; }

std::uint64_t BufferPool::available_bytes() const noexcept {
    std::uint64_t result = 0;
    for (const auto& list : lists_) {
        const auto header = read_buffer_list_header(memory_ + list.offset,
                                                    list.region_size);
        if (!header || header.value.size <= 1) {
            continue;
        }
        result += static_cast<std::uint64_t>(header.value.size - 1) *
                  header.value.capacity_per_buffer;
    }
    return result;
}

bool BufferPool::all_returned() const noexcept {
    for (const auto& list : lists_) {
        const auto* const list_memory = memory_ + list.offset;
        const auto header = read_buffer_list_header(list_memory, list.region_size);
        if (!header || header.value.size !=
                           static_cast<std::int32_t>(header.value.capacity) ||
            role_counter(list_memory, role_) != 0) {
            return false;
        }
    }
    return true;
}

BufferAllocationResult BufferPool::allocate(std::uint32_t size) noexcept {
    if (size == 0 || size > max_slice_capacity()) {
        return {{}, BufferPoolError::no_buffer};
    }
    for (std::size_t index = 0; index < lists_.size(); ++index) {
        const auto& list = lists_[index];
        if (size > list.capacity_per_buffer) {
            continue;
        }
        auto* const list_memory = memory_ + list.offset;
        const auto decoded = read_buffer_list_header(list_memory, list.region_size);
        if (!decoded) {
            return {{}, translate_layout_error(decoded.error)};
        }
        auto header = decoded.value;
        if (header.size <= 1) {
            continue;
        }
        const auto counter = role_counter(list_memory, role_);
        if (counter < 0) {
            return {{}, BufferPoolError::invalid_layout};
        }
        if (counter == std::numeric_limits<std::int32_t>::max()) {
            return {{}, BufferPoolError::counter_overflow};
        }
        const auto slice_relative = static_cast<std::size_t>(header.head);
        const auto stride = buffer_slice_header_size +
                            static_cast<std::size_t>(header.capacity_per_buffer);
        const auto buffer_bytes = list.region_size - buffer_list_header_size;
        const auto valid_relative = [stride, buffer_bytes](std::size_t offset) {
            return offset < buffer_bytes && offset % stride == 0U;
        };
        if (!valid_relative(slice_relative)) {
            return {{}, BufferPoolError::invalid_layout};
        }
        const auto slice_absolute = list.offset + buffer_list_header_size +
                                    slice_relative;
        const auto slice = read_buffer_slice_header(memory_ + slice_absolute,
                                                     size_ - slice_absolute);
        if (!slice || slice.value.capacity != header.capacity_per_buffer ||
            (slice.value.flags & (has_next_flag | in_use_flag)) != has_next_flag ||
            slice.value.size != 0U || slice.value.data_start != 0U ||
            !valid_relative(slice.value.next_offset)) {
            return {{}, BufferPoolError::invalid_layout};
        }

        auto allocated_header = slice.value;
        allocated_header.size = 0;
        allocated_header.data_start = 0;
        allocated_header.flags = in_use_flag;
        header.head = slice.value.next_offset;
        --header.size;
        if (write_buffer_slice_header(memory_ + slice_absolute,
                                      size_ - slice_absolute,
                                      allocated_header) != BufferLayoutError::none ||
            write_buffer_list_header(list_memory, list.region_size, header) !=
                BufferLayoutError::none) {
            return {{}, BufferPoolError::invalid_layout};
        }
        set_role_counter(list_memory, role_, counter + 1);
        return {BufferAllocation(
                    memory_, memory_ + slice_absolute + buffer_slice_header_size,
                    header.capacity_per_buffer,
                    static_cast<std::uint32_t>(slice_absolute), index, role_),
                BufferPoolError::none};
    }
    return {{}, BufferPoolError::no_buffer};
}

BufferPoolError BufferPool::recycle(BufferAllocation&& allocation) noexcept {
    if (!allocation || allocation.owner_ != memory_ || allocation.role_ != role_ ||
        allocation.list_index_ >= lists_.size()) {
        return BufferPoolError::invalid_allocation;
    }
    const auto& list = lists_[allocation.list_index_];
    const auto first_slice = list.offset + buffer_list_header_size;
    if (allocation.offset_ < first_slice || allocation.offset_ >= size_) {
        return BufferPoolError::invalid_allocation;
    }
    const auto relative = static_cast<std::size_t>(allocation.offset_) - first_slice;
    const auto stride = buffer_slice_header_size +
                        static_cast<std::size_t>(list.capacity_per_buffer);
    if (relative % stride != 0U || relative / stride >=
                                     (list.region_size - buffer_list_header_size) /
                                         stride) {
        return BufferPoolError::invalid_allocation;
    }

    auto* const list_memory = memory_ + list.offset;
    const auto decoded = read_buffer_list_header(list_memory, list.region_size);
    const auto slice = read_buffer_slice_header(memory_ + allocation.offset_,
                                                size_ - allocation.offset_);
    if (!decoded || !slice || slice.value.capacity != allocation.capacity_ ||
        slice.value.capacity != list.capacity_per_buffer) {
        return BufferPoolError::invalid_layout;
    }
    if ((slice.value.flags & in_use_flag) == 0U) {
        return BufferPoolError::allocation_not_in_use;
    }
    auto header = decoded.value;
    const auto counter = role_counter(list_memory, role_);
    if (header.size < 1 ||
        static_cast<std::uint32_t>(header.size) >= header.capacity || counter <= 0) {
        return BufferPoolError::invalid_layout;
    }
    const auto tail_relative = static_cast<std::size_t>(header.tail);
    if (tail_relative >= list.region_size - buffer_list_header_size ||
        tail_relative % stride != 0U) {
        return BufferPoolError::invalid_layout;
    }
    const auto tail_absolute = first_slice + tail_relative;
    const auto tail = read_buffer_slice_header(memory_ + tail_absolute,
                                               size_ - tail_absolute);
    if (!tail || tail.value.capacity != list.capacity_per_buffer ||
        (tail.value.flags & (has_next_flag | in_use_flag)) != 0U) {
        return BufferPoolError::invalid_layout;
    }

    BufferSliceHeader reset_slice{allocation.capacity_, 0, 0, 0, 0};
    auto linked_tail = tail.value;
    linked_tail.next_offset = static_cast<std::uint32_t>(relative);
    linked_tail.flags = has_next_flag;
    header.tail = static_cast<std::uint32_t>(relative);
    ++header.size;
    if (write_buffer_slice_header(memory_ + allocation.offset_,
                                  size_ - allocation.offset_, reset_slice) !=
            BufferLayoutError::none ||
        write_buffer_slice_header(memory_ + tail_absolute,
                                  size_ - tail_absolute, linked_tail) !=
            BufferLayoutError::none ||
        write_buffer_list_header(list_memory, list.region_size, header) !=
            BufferLayoutError::none) {
        return BufferPoolError::invalid_layout;
    }
    set_role_counter(list_memory, role_, counter - 1);
    allocation.invalidate();
    return BufferPoolError::none;
}

const char* to_string(BufferPoolError error) noexcept {
    switch (error) {
        case BufferPoolError::none:
            return "none";
        case BufferPoolError::null_memory:
            return "null memory";
        case BufferPoolError::invalid_config:
            return "invalid configuration";
        case BufferPoolError::size_overflow:
            return "size overflow";
        case BufferPoolError::truncated_region:
            return "truncated region";
        case BufferPoolError::invalid_layout:
            return "invalid layout";
        case BufferPoolError::no_buffer:
            return "no buffer";
        case BufferPoolError::invalid_allocation:
            return "invalid allocation";
        case BufferPoolError::allocation_not_in_use:
            return "allocation not in use";
        case BufferPoolError::counter_overflow:
            return "counter overflow";
    }
    return "unknown buffer pool error";
}

BufferPoolCreateResult initialize_buffer_pool(
    std::uint8_t* memory, std::size_t size, std::vector<BufferTierSpec> tiers,
    BufferListRole role) {
    if (memory == nullptr) {
        return {{}, BufferPoolError::null_memory};
    }
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        return {{}, BufferPoolError::size_overflow};
    }
    std::sort(tiers.begin(), tiers.end(),
              [](const auto& left, const auto& right) {
                  return left.capacity < right.capacity;
              });
    if (!valid_tiers(tiers)) {
        return {{}, BufferPoolError::invalid_config};
    }
    const auto list_headers_size = tiers.size() * buffer_list_header_size;
    if (size <= buffer_manager_header_size + list_headers_size) {
        return {{}, BufferPoolError::truncated_region};
    }
    const auto buffer_region_size =
        size - buffer_manager_header_size - list_headers_size;
    std::vector<BufferPool::ListView> lists;
    lists.reserve(tiers.size());
    std::size_t offset = buffer_manager_header_size;

    for (const auto& tier : tiers) {
        const auto tier_bytes = static_cast<std::uint64_t>(buffer_region_size) *
                                tier.percent / 100U;
        const auto stride = static_cast<std::uint64_t>(tier.capacity) +
                            buffer_slice_header_size;
        const auto count64 = tier_bytes / stride;
        if (count64 == 0 ||
            count64 > static_cast<std::uint64_t>(
                          std::numeric_limits<std::int32_t>::max())) {
            return {{}, BufferPoolError::invalid_config};
        }
        const auto count = static_cast<std::uint32_t>(count64);
        const auto list_size = buffer_list_region_size(count, tier.capacity);
        if (!list_size) {
            return {{}, translate_layout_error(list_size.error)};
        }
        if (offset > size || list_size.value > size - offset) {
            return {{}, BufferPoolError::truncated_region};
        }
        lists.push_back({offset, list_size.value, tier.capacity});
        offset += list_size.value;
    }
    if (offset - buffer_manager_header_size >
        std::numeric_limits<std::uint32_t>::max()) {
        return {{}, BufferPoolError::size_overflow};
    }

    std::memset(memory, 0, size);
    for (const auto& list : lists) {
        const auto count = static_cast<std::uint32_t>(
            (list.region_size - buffer_list_header_size) /
            (buffer_slice_header_size + list.capacity_per_buffer));
        const auto stride = buffer_slice_header_size + list.capacity_per_buffer;
        const BufferListHeader list_header{
            static_cast<std::int32_t>(count), count, 0,
            static_cast<std::uint32_t>((count - 1U) * stride),
            list.capacity_per_buffer, 0, 0};
        if (write_buffer_list_header(memory + list.offset, list.region_size,
                                     list_header) != BufferLayoutError::none) {
            return {{}, BufferPoolError::invalid_layout};
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto relative = index * stride;
            const BufferSliceHeader slice{
                list.capacity_per_buffer, 0, 0,
                index + 1U < count
                    ? static_cast<std::uint32_t>(relative + stride)
                    : 0U,
                static_cast<std::uint8_t>(index + 1U < count ? has_next_flag : 0U)};
            const auto absolute = list.offset + buffer_list_header_size + relative;
            if (write_buffer_slice_header(memory + absolute, size - absolute,
                                          slice) != BufferLayoutError::none) {
                return {{}, BufferPoolError::invalid_layout};
            }
        }
    }
    const BufferManagerHeader manager_header{
        static_cast<std::uint16_t>(lists.size()),
        static_cast<std::uint32_t>(offset - buffer_manager_header_size)};
    if (write_buffer_manager_header(memory, size, manager_header) !=
        BufferLayoutError::none) {
        return {{}, BufferPoolError::invalid_layout};
    }
    return {BufferPool(memory, size, offset, role, std::move(lists)),
            BufferPoolError::none};
}

BufferPoolCreateResult map_buffer_pool(std::uint8_t* memory, std::size_t size,
                                       BufferListRole role) {
    if (memory == nullptr) {
        return {{}, BufferPoolError::null_memory};
    }
    const auto manager = read_buffer_manager_header(memory, size);
    if (!manager) {
        return {{}, translate_layout_error(manager.error)};
    }
    const auto manager_end = buffer_manager_header_size +
                             static_cast<std::size_t>(manager.value.used_length);
    if (manager_end > std::numeric_limits<std::uint32_t>::max()) {
        return {{}, BufferPoolError::size_overflow};
    }
    std::vector<BufferPool::ListView> lists;
    lists.reserve(manager.value.list_count);
    std::size_t offset = buffer_manager_header_size;
    std::uint32_t previous_capacity = 0;
    for (std::uint16_t index = 0; index < manager.value.list_count; ++index) {
        if (offset >= manager_end) {
            return {{}, BufferPoolError::truncated_region};
        }
        const auto header = read_buffer_list_header(memory + offset,
                                                    manager_end - offset);
        if (!header) {
            return {{}, translate_layout_error(header.error)};
        }
        if (header.value.capacity_per_buffer <= previous_capacity) {
            return {{}, BufferPoolError::invalid_layout};
        }
        if (header.value.capacity > static_cast<std::uint32_t>(
                                        std::numeric_limits<std::int32_t>::max()) ||
            header.value.size < 1) {
            return {{}, BufferPoolError::invalid_layout};
        }
        const auto list_size = buffer_list_region_size(
            header.value.capacity, header.value.capacity_per_buffer);
        if (!list_size || list_size.value > manager_end - offset) {
            return {{}, list_size ? BufferPoolError::truncated_region
                                  : translate_layout_error(list_size.error)};
        }
        const auto chain_error = validate_buffer_list_chain(memory + offset,
                                                            list_size.value);
        if (chain_error != BufferLayoutError::none) {
            return {{}, translate_layout_error(chain_error)};
        }
        const auto stride = buffer_slice_header_size +
                            static_cast<std::size_t>(
                                header.value.capacity_per_buffer);
        auto current = header.value.head;
        std::uint32_t free_count = 0;
        for (;;) {
            const auto slice_offset = buffer_list_header_size +
                                      static_cast<std::size_t>(current);
            const auto slice = read_buffer_slice_header(
                memory + offset + slice_offset, list_size.value - slice_offset);
            if (!slice) {
                return {{}, translate_layout_error(slice.error)};
            }
            if ((slice.value.flags & in_use_flag) != 0U ||
                slice.value.size != 0U || slice.value.data_start != 0U) {
                return {{}, BufferPoolError::invalid_layout};
            }
            ++free_count;
            if ((slice.value.flags & has_next_flag) == 0U) {
                break;
            }
            current = slice.value.next_offset;
            if (free_count >= header.value.capacity ||
                static_cast<std::size_t>(current) % stride != 0U) {
                return {{}, BufferPoolError::invalid_layout};
            }
        }
        if (free_count != static_cast<std::uint32_t>(header.value.size)) {
            return {{}, BufferPoolError::invalid_layout};
        }
        lists.push_back({offset, list_size.value,
                         header.value.capacity_per_buffer});
        previous_capacity = header.value.capacity_per_buffer;
        offset += list_size.value;
    }
    if (offset != manager_end) {
        return {{}, BufferPoolError::invalid_layout};
    }
    return {BufferPool(memory, size, manager_end, role, std::move(lists)),
            BufferPoolError::none};
}

}  // namespace shmipc::shm
