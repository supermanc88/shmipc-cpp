#include "shm/buffer_pool.hpp"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

namespace {

using shmipc::shm::BufferAllocation;
using shmipc::shm::BufferListRole;
using shmipc::shm::BufferManagerHeader;
using shmipc::shm::BufferPoolError;
using shmipc::shm::BufferTierSpec;

constexpr std::size_t region_size = 1U << 20U;

bool test_invalid_config() {
    std::vector<std::uint8_t> memory(region_size, 0);
    const auto null_memory = shmipc::shm::initialize_buffer_pool(
        nullptr, memory.size(), {{4096U, 100U}});
    const auto no_tiers = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {});
    const auto bad_percent = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{4096U, 60U}, {8192U, 30U}});
    const auto duplicate = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{4096U, 50U}, {4096U, 50U}});
    const auto too_small = shmipc::shm::initialize_buffer_pool(
        memory.data(), 64U, {{4096U, 50U}, {8192U, 50U}});
    return null_memory.error == BufferPoolError::null_memory &&
           no_tiers.error == BufferPoolError::invalid_config &&
           bad_percent.error == BufferPoolError::invalid_config &&
           duplicate.error == BufferPoolError::invalid_config &&
           too_small.error == BufferPoolError::truncated_region;
}

bool test_initialize_map_and_roles() {
    std::vector<std::uint8_t> memory(region_size, 0);
    auto creator = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{8192U, 50U}, {4096U, 50U}},
        BufferListRole::creator);
    if (!creator || creator.value.list_count() != 2U ||
        creator.value.min_slice_capacity() != 4096U ||
        creator.value.max_slice_capacity() != 8192U ||
        creator.value.used_size() > memory.size() ||
        !creator.value.all_returned()) {
        return false;
    }
    const auto manager =
        shmipc::shm::read_buffer_manager_header(memory.data(), memory.size());
    if (!manager || manager.value.list_count != 2U ||
        manager.value.used_length + shmipc::shm::buffer_manager_header_size !=
            creator.value.used_size()) {
        return false;
    }

    auto mapper = shmipc::shm::map_buffer_pool(
        memory.data(), memory.size(), BufferListRole::mapper);
    if (!mapper || mapper.value.available_bytes() !=
                       creator.value.available_bytes()) {
        return false;
    }
    auto creator_buffer = creator.value.allocate(100U);
    auto mapper_buffer = mapper.value.allocate(100U);
    if (!creator_buffer || !mapper_buffer ||
        creator_buffer.value.capacity() != 4096U ||
        mapper_buffer.value.capacity() != 4096U ||
        creator.value.all_returned() || mapper.value.all_returned()) {
        return false;
    }
    creator_buffer.value.data()[0] = 0x5aU;
    if (memory[creator_buffer.value.offset() +
               shmipc::shm::buffer_slice_header_size] != 0x5aU) {
        return false;
    }
    const auto first_list = shmipc::shm::read_buffer_list_header(
        memory.data() + shmipc::shm::buffer_manager_header_size,
        memory.size() - shmipc::shm::buffer_manager_header_size);
    if (!first_list || first_list.value.creator_counter != 1 ||
        first_list.value.mapper_counter != 1) {
        return false;
    }
    if (mapper.value.recycle(std::move(creator_buffer.value)) !=
            BufferPoolError::invalid_allocation ||
        !creator_buffer.value ||
        creator.value.recycle(std::move(creator_buffer.value)) !=
            BufferPoolError::none ||
        mapper.value.recycle(std::move(mapper_buffer.value)) !=
            BufferPoolError::none ||
        creator_buffer.value || mapper_buffer.value ||
        !creator.value.all_returned() || !mapper.value.all_returned()) {
        return false;
    }
    return creator.value.recycle(std::move(creator_buffer.value)) ==
           BufferPoolError::invalid_allocation;
}

bool test_selection_exhaustion_and_recycle() {
    std::vector<std::uint8_t> memory(region_size, 0);
    auto pool = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{4096U, 50U}, {8192U, 50U}});
    if (!pool) {
        return false;
    }
    const auto initial_bytes = pool.value.available_bytes();
    auto large = pool.value.allocate(5000U);
    if (!large || large.value.capacity() != 8192U ||
        pool.value.available_bytes() != initial_bytes - 8192U) {
        return false;
    }
    if (pool.value.recycle(std::move(large.value)) != BufferPoolError::none) {
        return false;
    }

    std::vector<BufferAllocation> allocations;
    bool used_larger_tier = false;
    for (;;) {
        auto allocation = pool.value.allocate(4096U);
        if (!allocation) {
            if (allocation.error != BufferPoolError::no_buffer) {
                return false;
            }
            break;
        }
        used_larger_tier = used_larger_tier || allocation.value.capacity() == 8192U;
        allocations.push_back(std::move(allocation.value));
    }
    if (!used_larger_tier || pool.value.available_bytes() != 0U) {
        return false;
    }
    for (auto& allocation : allocations) {
        if (pool.value.recycle(std::move(allocation)) != BufferPoolError::none) {
            return false;
        }
    }
    return pool.value.all_returned() &&
           pool.value.available_bytes() == initial_bytes;
}

bool test_wrong_pool_and_corrupt_manager() {
    std::vector<std::uint8_t> first_memory(region_size, 0);
    std::vector<std::uint8_t> second_memory(region_size, 0);
    auto first = shmipc::shm::initialize_buffer_pool(
        first_memory.data(), first_memory.size(), {{4096U, 100U}});
    auto second = shmipc::shm::initialize_buffer_pool(
        second_memory.data(), second_memory.size(), {{4096U, 100U}});
    if (!first || !second) {
        return false;
    }
    auto allocation = first.value.allocate(1U);
    if (!allocation ||
        second.value.recycle(std::move(allocation.value)) !=
            BufferPoolError::invalid_allocation ||
        !allocation.value) {
        return false;
    }

    auto allocated_list = shmipc::shm::read_buffer_list_header(
        first_memory.data() + shmipc::shm::buffer_manager_header_size,
        first_memory.size() - shmipc::shm::buffer_manager_header_size);
    if (!allocated_list) {
        return false;
    }
    auto bad_tail = allocated_list.value;
    bad_tail.tail = static_cast<std::uint32_t>(first_memory.size());
    if (shmipc::shm::write_buffer_list_header(
            first_memory.data() + shmipc::shm::buffer_manager_header_size,
            first_memory.size() - shmipc::shm::buffer_manager_header_size,
            bad_tail) != shmipc::shm::BufferLayoutError::none ||
        first.value.recycle(std::move(allocation.value)) !=
            BufferPoolError::invalid_layout ||
        !allocation.value ||
        shmipc::shm::write_buffer_list_header(
            first_memory.data() + shmipc::shm::buffer_manager_header_size,
            first_memory.size() - shmipc::shm::buffer_manager_header_size,
            allocated_list.value) != shmipc::shm::BufferLayoutError::none ||
        first.value.recycle(std::move(allocation.value)) != BufferPoolError::none) {
        return false;
    }

    auto bad_size_memory = first_memory;
    auto full_list = shmipc::shm::read_buffer_list_header(
        bad_size_memory.data() + shmipc::shm::buffer_manager_header_size,
        bad_size_memory.size() - shmipc::shm::buffer_manager_header_size);
    if (!full_list) {
        return false;
    }
    --full_list.value.size;
    if (shmipc::shm::write_buffer_list_header(
            bad_size_memory.data() + shmipc::shm::buffer_manager_header_size,
            bad_size_memory.size() - shmipc::shm::buffer_manager_header_size,
            full_list.value) != shmipc::shm::BufferLayoutError::none ||
        shmipc::shm::map_buffer_pool(bad_size_memory.data(),
                                     bad_size_memory.size(),
                                     BufferListRole::mapper)
                .error != BufferPoolError::invalid_layout) {
        return false;
    }

    auto bad_length_memory = first_memory;
    const auto manager = shmipc::shm::read_buffer_manager_header(
        bad_length_memory.data(), bad_length_memory.size());
    if (!manager) {
        return false;
    }
    const BufferManagerHeader corrupt{manager.value.list_count,
                                      manager.value.used_length + 1U};
    if (shmipc::shm::write_buffer_manager_header(
            bad_length_memory.data(), bad_length_memory.size(), corrupt) !=
        shmipc::shm::BufferLayoutError::none) {
        return false;
    }
    return shmipc::shm::map_buffer_pool(
               bad_length_memory.data(), bad_length_memory.size(),
               BufferListRole::mapper)
               .error == BufferPoolError::invalid_layout;
}

}  // namespace

int main() {
    if (!test_invalid_config()) {
        std::cerr << "buffer pool configuration test failed\n";
        return 1;
    }
    if (!test_initialize_map_and_roles()) {
        std::cerr << "buffer pool mapping/role test failed\n";
        return 1;
    }
    if (!test_selection_exhaustion_and_recycle()) {
        std::cerr << "buffer pool allocation/recycle test failed\n";
        return 1;
    }
    if (!test_wrong_pool_and_corrupt_manager()) {
        std::cerr << "buffer pool invalid ownership/layout test failed\n";
        return 1;
    }
    return 0;
}
