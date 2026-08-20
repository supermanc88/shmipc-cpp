#include "shm/buffer_pool.hpp"

#include "shm/atomic_word.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace shmipc::shm {
namespace {

constexpr std::uint8_t has_next_flag = 1U;
constexpr std::uint8_t in_use_flag = 2U;

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
    const auto* const counter = reinterpret_cast<const std::int32_t*>(
        list + buffer_list_counter_offset(role));
    return detail::atomic_load(counter);
}

std::int32_t add_role_counter(std::uint8_t* list, BufferListRole role,
                              std::int32_t value) noexcept {
    auto* const counter = reinterpret_cast<std::int32_t*>(
        list + buffer_list_counter_offset(role));
    return detail::atomic_fetch_add(counter, value);
}

std::int32_t* list_size_word(std::uint8_t* list) noexcept {
    return reinterpret_cast<std::int32_t*>(list);
}

const std::int32_t* list_size_word(const std::uint8_t* list) noexcept {
    return reinterpret_cast<const std::int32_t*>(list);
}

std::uint32_t* list_head_word(std::uint8_t* list) noexcept {
    return reinterpret_cast<std::uint32_t*>(list + 8U);
}

std::uint32_t* list_tail_word(std::uint8_t* list) noexcept {
    return reinterpret_cast<std::uint32_t*>(list + 12U);
}

bool atomics_aligned(const std::uint8_t* list) noexcept {
    return detail::atomic_word_aligned(list_size_word(list)) &&
           detail::atomic_word_aligned(
               reinterpret_cast<const std::uint32_t*>(list + 8U)) &&
           detail::atomic_word_aligned(
               reinterpret_cast<const std::uint32_t*>(list + 12U)) &&
           detail::atomic_word_aligned(
               reinterpret_cast<const std::int32_t*>(list + 20U)) &&
           detail::atomic_word_aligned(
               reinterpret_cast<const std::int32_t*>(list + 24U));
}

bool valid_tiers(const std::vector<BufferTierSpec>& tiers) noexcept {
    if (tiers.empty() || tiers.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    std::uint32_t percent_sum = 0;
    std::uint32_t previous_capacity = 0;
    for (const auto& tier : tiers) {
        if (tier.capacity == 0 || tier.capacity % 4U != 0U ||
            tier.percent == 0 || tier.percent > 100U ||
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

std::uint64_t BufferPool::capacity_bytes() const noexcept {
    std::uint64_t result = 0U;
    for (const auto& list : lists_) {
        result += static_cast<std::uint64_t>(list.capacity) *
                  list.capacity_per_buffer;
    }
    return result;
}

std::uint64_t BufferPool::used_bytes() const noexcept {
    std::uint64_t result = 0U;
    for (const auto& list : lists_) {
        const auto* const list_memory = memory_ + list.offset;
        const auto free_count = detail::atomic_load(list_size_word(list_memory));
        if (free_count < 0 ||
            static_cast<std::uint32_t>(free_count) > list.capacity) {
            continue;
        }
        result += static_cast<std::uint64_t>(
                      list.capacity - static_cast<std::uint32_t>(free_count)) *
                  list.capacity_per_buffer;
    }
    return result;
}

std::uint64_t BufferPool::available_bytes() const noexcept {
    std::uint64_t result = 0;
    for (const auto& list : lists_) {
        const auto* const list_memory = memory_ + list.offset;
        const auto free_count = detail::atomic_load(list_size_word(list_memory));
        if (free_count <= 1) {
            continue;
        }
        result += static_cast<std::uint64_t>(free_count - 1) *
                  list.capacity_per_buffer;
    }
    return result;
}

bool BufferPool::all_returned() const noexcept {
    for (const auto& list : lists_) {
        const auto* const list_memory = memory_ + list.offset;
        if (detail::atomic_load(list_size_word(list_memory)) !=
                static_cast<std::int32_t>(list.capacity) ||
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
        const auto remaining = detail::atomic_fetch_add(
                                   list_size_word(list_memory),
                                   static_cast<std::int32_t>(-1)) -
                               1;
        if (remaining <= 0) {
            static_cast<void>(detail::atomic_fetch_add(
                list_size_word(list_memory), static_cast<std::int32_t>(1)));
            continue;
        }
        const auto counter = role_counter(list_memory, role_);
        if (counter == std::numeric_limits<std::int32_t>::max()) {
            static_cast<void>(detail::atomic_fetch_add(
                list_size_word(list_memory), static_cast<std::int32_t>(1)));
            return {{}, BufferPoolError::counter_overflow};
        }
        const auto stride = buffer_slice_header_size +
                            static_cast<std::size_t>(list.capacity_per_buffer);
        const auto buffer_bytes = list.region_size - buffer_list_header_size;
        const auto valid_relative = [stride, buffer_bytes](std::size_t offset) {
            return offset < buffer_bytes && offset % stride == 0U;
        };
        auto expected_head = detail::atomic_load(list_head_word(list_memory));
        constexpr int maximum_retries = 200;
        for (int retry = 0; retry < maximum_retries; ++retry) {
            const auto slice_relative = static_cast<std::size_t>(expected_head);
            if (!valid_relative(slice_relative)) {
                static_cast<void>(detail::atomic_fetch_add(
                    list_size_word(list_memory), static_cast<std::int32_t>(1)));
                return {{}, BufferPoolError::invalid_layout};
            }
            const auto slice_absolute = list.offset + buffer_list_header_size +
                                        slice_relative;
            const auto slice = read_buffer_slice_header(memory_ + slice_absolute,
                                                         size_ - slice_absolute);
            if (!slice || slice.value.capacity != list.capacity_per_buffer) {
                static_cast<void>(detail::atomic_fetch_add(
                    list_size_word(list_memory), static_cast<std::int32_t>(1)));
                return {{}, BufferPoolError::invalid_layout};
            }
            if ((slice.value.flags & (has_next_flag | in_use_flag)) !=
                    has_next_flag ||
                slice.value.size != 0U || slice.value.data_start != 0U) {
                expected_head = detail::atomic_load(list_head_word(list_memory));
                continue;
            }
            if (!valid_relative(slice.value.next_offset)) {
                static_cast<void>(detail::atomic_fetch_add(
                    list_size_word(list_memory), static_cast<std::int32_t>(1)));
                return {{}, BufferPoolError::invalid_layout};
            }
            auto observed_head = expected_head;
            if (detail::atomic_compare_exchange(list_head_word(list_memory),
                                                observed_head,
                                                slice.value.next_offset)) {
                auto allocated_header = slice.value;
                allocated_header.size = 0;
                allocated_header.data_start = 0;
                allocated_header.flags = in_use_flag;
                if (write_buffer_slice_header(memory_ + slice_absolute,
                                              size_ - slice_absolute,
                                              allocated_header) !=
                    BufferLayoutError::none) {
                    return {{}, BufferPoolError::invalid_layout};
                }
                static_cast<void>(add_role_counter(list_memory, role_, 1));
                return {BufferAllocation(
                            memory_,
                            memory_ + slice_absolute + buffer_slice_header_size,
                            list.capacity_per_buffer,
                            static_cast<std::uint32_t>(slice_absolute), index,
                            role_),
                        BufferPoolError::none};
            }
            expected_head = observed_head;
        }
        static_cast<void>(detail::atomic_fetch_add(
            list_size_word(list_memory), static_cast<std::int32_t>(1)));
    }
    return {{}, BufferPoolError::no_buffer};
}

BufferChainResult BufferPool::allocate_chain(std::uint64_t size) {
    if (size == 0 || max_slice_capacity() == 0U) {
        return {{}, BufferPoolError::no_buffer};
    }
    BufferChain chain;
    auto remaining = size;
    for (auto list = lists_.rbegin(); list != lists_.rend() && remaining > 0U;
         ++list) {
        for (;;) {
            auto allocation = allocate(list->capacity_per_buffer);
            if (!allocation) {
                if (allocation.error != BufferPoolError::no_buffer) {
                    const auto original_error = allocation.error;
                    if (recycle_chain(std::move(chain)) != BufferPoolError::none) {
                        return {{}, BufferPoolError::invalid_layout};
                    }
                    return {{}, original_error};
                }
                break;
            }
            remaining = remaining > allocation.value.capacity()
                            ? remaining - allocation.value.capacity()
                            : 0U;
            try {
                chain.allocations.push_back(std::move(allocation.value));
            } catch (...) {
                static_cast<void>(recycle(std::move(allocation.value)));
                static_cast<void>(recycle_chain(std::move(chain)));
                throw;
            }
            if (remaining == 0U) {
                break;
            }
        }
    }
    if (remaining > 0U) {
        if (recycle_chain(std::move(chain)) != BufferPoolError::none) {
            return {{}, BufferPoolError::invalid_layout};
        }
        return {{}, BufferPoolError::no_buffer};
    }
    chain.data_size = size;
    return {std::move(chain), BufferPoolError::none};
}

PublishedBufferChainResult BufferPool::publish_chain(
    BufferChain&& chain,
    const std::vector<std::uint32_t>& slice_sizes) noexcept {
    if (chain.allocations.empty() ||
        slice_sizes.size() != chain.allocations.size()) {
        return {{}, BufferPoolError::invalid_allocation};
    }
    std::uint64_t data_size = 0;
    for (std::size_t index = 0; index < chain.allocations.size(); ++index) {
        const auto& allocation = chain.allocations[index];
        if (!allocation || allocation.owner_ != memory_ ||
            allocation.role_ != role_ || slice_sizes[index] > allocation.capacity_) {
            return {{}, BufferPoolError::invalid_allocation};
        }
        data_size += slice_sizes[index];
        const auto header = read_buffer_slice_header(
            memory_ + allocation.offset_, size_ - allocation.offset_);
        if (!header || header.value.capacity != allocation.capacity_ ||
            (header.value.flags & in_use_flag) == 0U) {
            return {{}, BufferPoolError::invalid_layout};
        }
    }

    for (std::size_t index = 0; index < chain.allocations.size(); ++index) {
        const auto& allocation = chain.allocations[index];
        const auto has_next = index + 1U < chain.allocations.size();
        const BufferSliceHeader header{
            allocation.capacity_, slice_sizes[index], 0,
            has_next ? chain.allocations[index + 1U].offset_ : 0U,
            static_cast<std::uint8_t>(in_use_flag |
                                      (has_next ? has_next_flag : 0U))};
        if (write_buffer_slice_header(memory_ + allocation.offset_,
                                      size_ - allocation.offset_, header) !=
            BufferLayoutError::none) {
            return {{}, BufferPoolError::invalid_layout};
        }
    }

    const PublishedBufferChain published{chain.allocations.front().offset_,
                                         chain.allocations.size(), data_size};
    for (auto& allocation : chain.allocations) {
        allocation.invalidate();
    }
    chain.allocations.clear();
    chain.data_size = 0;
    return {published, BufferPoolError::none};
}

BufferChainResult BufferPool::adopt_chain(std::uint32_t root_offset) const {
    if (root_offset == 0U) {
        return {{}, BufferPoolError::invalid_allocation};
    }
    std::uint64_t maximum_slices = 0;
    for (const auto& list : lists_) {
        maximum_slices += list.capacity;
    }

    BufferChain chain;
    auto current = root_offset;
    for (std::uint64_t visited = 0; visited < maximum_slices; ++visited) {
        std::size_t list_index = lists_.size();
        for (std::size_t index = 0; index < lists_.size(); ++index) {
            const auto& list = lists_[index];
            const auto first_slice = list.offset + buffer_list_header_size;
            const auto stride = buffer_slice_header_size +
                                static_cast<std::size_t>(
                                    list.capacity_per_buffer);
            if (current >= first_slice) {
                const auto relative = static_cast<std::size_t>(current) - first_slice;
                if (relative % stride == 0U && relative / stride < list.capacity) {
                    list_index = index;
                    break;
                }
            }
        }
        if (list_index == lists_.size()) {
            return {{}, BufferPoolError::invalid_allocation};
        }
        if (std::any_of(chain.allocations.begin(), chain.allocations.end(),
                        [current](const auto& allocation) {
                            return allocation.offset() == current;
                        })) {
            return {{}, BufferPoolError::invalid_layout};
        }
        const auto& list = lists_[list_index];
        const auto header = read_buffer_slice_header(memory_ + current,
                                                     size_ - current);
        if (!header || header.value.capacity != list.capacity_per_buffer ||
            (header.value.flags & in_use_flag) == 0U) {
            return {{}, BufferPoolError::invalid_layout};
        }
        chain.data_size += header.value.size;
        BufferAllocation allocation(
            memory_, memory_ + current + buffer_slice_header_size,
            header.value.capacity, current, list_index, role_);
        chain.allocations.push_back(std::move(allocation));
        if ((header.value.flags & has_next_flag) == 0U) {
            return {std::move(chain), BufferPoolError::none};
        }
        current = header.value.next_offset;
    }
    return {{}, BufferPoolError::invalid_layout};
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
    const auto slice = read_buffer_slice_header(memory_ + allocation.offset_,
                                                size_ - allocation.offset_);
    if (!slice || slice.value.capacity != allocation.capacity_ ||
        slice.value.capacity != list.capacity_per_buffer) {
        return BufferPoolError::invalid_layout;
    }
    if ((slice.value.flags & in_use_flag) == 0U) {
        return BufferPoolError::allocation_not_in_use;
    }
    const auto counter = role_counter(list_memory, role_);
    const auto free_count = detail::atomic_load(list_size_word(list_memory));
    if (free_count < 1 ||
        static_cast<std::uint32_t>(free_count) >= list.capacity ||
        counter == std::numeric_limits<std::int32_t>::min()) {
        return BufferPoolError::invalid_layout;
    }

    auto expected_tail = detail::atomic_load(list_tail_word(list_memory));
    const auto initial_tail_relative = static_cast<std::size_t>(expected_tail);
    if (initial_tail_relative >= list.region_size - buffer_list_header_size ||
        initial_tail_relative % stride != 0U) {
        return BufferPoolError::invalid_layout;
    }
    BufferSliceHeader reset_slice{allocation.capacity_, 0, 0, 0, 0};
    if (write_buffer_slice_header(memory_ + allocation.offset_,
                                  size_ - allocation.offset_, reset_slice) !=
        BufferLayoutError::none) {
        return BufferPoolError::invalid_layout;
    }
    for (;;) {
        const auto tail_relative = static_cast<std::size_t>(expected_tail);
        if (tail_relative >= list.region_size - buffer_list_header_size ||
            tail_relative % stride != 0U) {
            return BufferPoolError::invalid_layout;
        }
        auto observed_tail = expected_tail;
        if (detail::atomic_compare_exchange(
                list_tail_word(list_memory), observed_tail,
                static_cast<std::uint32_t>(relative))) {
            const auto tail_absolute = first_slice + tail_relative;
            const auto tail = read_buffer_slice_header(memory_ + tail_absolute,
                                                       size_ - tail_absolute);
            if (!tail || tail.value.capacity != list.capacity_per_buffer ||
                (tail.value.flags & (has_next_flag | in_use_flag)) != 0U) {
                return BufferPoolError::invalid_layout;
            }
            auto linked_tail = tail.value;
            linked_tail.next_offset = static_cast<std::uint32_t>(relative);
            linked_tail.flags = has_next_flag;
            if (write_buffer_slice_header(memory_ + tail_absolute,
                                          size_ - tail_absolute, linked_tail) !=
                BufferLayoutError::none) {
                return BufferPoolError::invalid_layout;
            }
            break;
        }
        expected_tail = observed_tail;
    }
    static_cast<void>(detail::atomic_fetch_add(list_size_word(list_memory),
                                               static_cast<std::int32_t>(1)));
    static_cast<void>(add_role_counter(list_memory, role_, -1));
    allocation.invalidate();
    return BufferPoolError::none;
}

BufferPoolError BufferPool::recycle_chain(BufferChain&& chain) noexcept {
    for (auto& allocation : chain.allocations) {
        const auto error = recycle(std::move(allocation));
        if (error != BufferPoolError::none) {
            return error;
        }
    }
    chain.allocations.clear();
    chain.data_size = 0;
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
        case BufferPoolError::misaligned_atomic:
            return "misaligned atomic word";
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
        if (!atomics_aligned(memory + offset)) {
            return {{}, BufferPoolError::misaligned_atomic};
        }
        lists.push_back({offset, list_size.value, count, tier.capacity});
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
        if (header.value.capacity_per_buffer % 4U != 0U ||
            !atomics_aligned(memory + offset)) {
            return {{}, BufferPoolError::misaligned_atomic};
        }
        if (header.value.capacity > static_cast<std::uint32_t>(
                                        std::numeric_limits<std::int32_t>::max())) {
            return {{}, BufferPoolError::invalid_layout};
        }
        const auto list_size = buffer_list_region_size(
            header.value.capacity, header.value.capacity_per_buffer);
        if (!list_size || list_size.value > manager_end - offset) {
            return {{}, list_size ? BufferPoolError::truncated_region
                                  : translate_layout_error(list_size.error)};
        }
        const auto stride = buffer_slice_header_size +
                            static_cast<std::size_t>(
                                header.value.capacity_per_buffer);
        const auto buffer_bytes = list_size.value - buffer_list_header_size;
        const auto valid_dynamic_offset = [buffer_bytes, stride](
                                              std::uint32_t value) {
            return static_cast<std::size_t>(value) < buffer_bytes &&
                   static_cast<std::size_t>(value) % stride == 0U;
        };
        if (!valid_dynamic_offset(header.value.head) ||
            !valid_dynamic_offset(header.value.tail)) {
            return {{}, BufferPoolError::invalid_layout};
        }
        lists.push_back({offset, list_size.value, header.value.capacity,
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
