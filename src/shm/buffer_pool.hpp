#pragma once

#include "shm/buffer_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace shmipc::shm {

struct BufferTierSpec {
    std::uint32_t capacity;
    std::uint32_t percent;
};

enum class BufferPoolError {
    none,
    null_memory,
    invalid_config,
    size_overflow,
    truncated_region,
    invalid_layout,
    misaligned_atomic,
    no_buffer,
    invalid_allocation,
    allocation_not_in_use,
    counter_overflow,
};

template <typename T>
struct BufferPoolResult {
    T value{};
    BufferPoolError error{BufferPoolError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == BufferPoolError::none;
    }
};

class BufferAllocation final {
public:
    BufferAllocation() noexcept = default;
    BufferAllocation(const BufferAllocation&) = delete;
    BufferAllocation& operator=(const BufferAllocation&) = delete;
    BufferAllocation(BufferAllocation&& other) noexcept;
    BufferAllocation& operator=(BufferAllocation&& other) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint8_t* data() noexcept;
    [[nodiscard]] const std::uint8_t* data() const noexcept;
    [[nodiscard]] std::uint32_t capacity() const noexcept;
    [[nodiscard]] std::uint32_t offset() const noexcept;

private:
    friend class BufferPool;

    BufferAllocation(std::uint8_t* owner, std::uint8_t* data,
                     std::uint32_t capacity, std::uint32_t offset,
                     std::size_t list_index, BufferListRole role) noexcept;
    void invalidate() noexcept;

    std::uint8_t* owner_{nullptr};
    std::uint8_t* data_{nullptr};
    std::uint32_t capacity_{0};
    std::uint32_t offset_{0};
    std::size_t list_index_{0};
    BufferListRole role_{BufferListRole::creator};
};

struct BufferChain {
    std::vector<BufferAllocation> allocations{};
    std::uint64_t data_size{0};

    [[nodiscard]] explicit operator bool() const noexcept {
        return !allocations.empty();
    }
    [[nodiscard]] std::uint32_t root_offset() const noexcept {
        return allocations.empty() ? 0U : allocations.front().offset();
    }
};

struct PublishedBufferChain {
    std::uint32_t root_offset{0};
    std::size_t slice_count{0};
    std::uint64_t data_size{0};
};

class BufferPool final {
public:
    BufferPool() noexcept = default;

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;
    BufferPool(BufferPool&&) noexcept = default;
    BufferPool& operator=(BufferPool&&) noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::size_t list_count() const noexcept;
    [[nodiscard]] std::uint32_t min_slice_capacity() const noexcept;
    [[nodiscard]] std::uint32_t max_slice_capacity() const noexcept;
    [[nodiscard]] std::size_t used_size() const noexcept;
    [[nodiscard]] std::uint64_t available_bytes() const noexcept;
    [[nodiscard]] bool all_returned() const noexcept;

    [[nodiscard]] BufferPoolResult<BufferAllocation> allocate(
        std::uint32_t size) noexcept;
    [[nodiscard]] BufferPoolResult<BufferChain> allocate_chain(
        std::uint64_t size);
    [[nodiscard]] BufferPoolResult<PublishedBufferChain> publish_chain(
        BufferChain&& chain, const std::vector<std::uint32_t>& slice_sizes) noexcept;
    [[nodiscard]] BufferPoolResult<BufferChain> adopt_chain(
        std::uint32_t root_offset) const;
    [[nodiscard]] BufferPoolError recycle(
        BufferAllocation&& allocation) noexcept;
    [[nodiscard]] BufferPoolError recycle_chain(BufferChain&& chain) noexcept;

private:
    struct ListView {
        std::size_t offset;
        std::size_t region_size;
        std::uint32_t capacity;
        std::uint32_t capacity_per_buffer;
    };

    friend BufferPoolResult<BufferPool> initialize_buffer_pool(
        std::uint8_t*, std::size_t, std::vector<BufferTierSpec>,
        BufferListRole);
    friend BufferPoolResult<BufferPool> map_buffer_pool(
        std::uint8_t*, std::size_t, BufferListRole);

    BufferPool(std::uint8_t* memory, std::size_t size,
               std::size_t used_size, BufferListRole role,
               std::vector<ListView> lists) noexcept;

    std::uint8_t* memory_{nullptr};
    std::size_t size_{0};
    std::size_t used_size_{0};
    BufferListRole role_{BufferListRole::creator};
    std::vector<ListView> lists_{};
};

using BufferPoolCreateResult = BufferPoolResult<BufferPool>;
using BufferAllocationResult = BufferPoolResult<BufferAllocation>;
using BufferChainResult = BufferPoolResult<BufferChain>;
using PublishedBufferChainResult = BufferPoolResult<PublishedBufferChain>;

[[nodiscard]] const char* to_string(BufferPoolError error) noexcept;

[[nodiscard]] BufferPoolCreateResult initialize_buffer_pool(
    std::uint8_t* memory, std::size_t size,
    std::vector<BufferTierSpec> tiers,
    BufferListRole role = BufferListRole::creator);
[[nodiscard]] BufferPoolCreateResult map_buffer_pool(
    std::uint8_t* memory, std::size_t size, BufferListRole role);

}  // namespace shmipc::shm
