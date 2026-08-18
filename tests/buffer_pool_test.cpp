#include "shm/buffer_pool.hpp"
#include "shm/shared_memory_region.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>

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
    const auto misaligned_capacity = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{4095U, 100U}});
    const auto too_small = shmipc::shm::initialize_buffer_pool(
        memory.data(), 64U, {{4096U, 50U}, {8192U, 50U}});
    return null_memory.error == BufferPoolError::null_memory &&
           no_tiers.error == BufferPoolError::invalid_config &&
           bad_percent.error == BufferPoolError::invalid_config &&
           duplicate.error == BufferPoolError::invalid_config &&
           misaligned_capacity.error == BufferPoolError::invalid_config &&
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

bool test_map_live_pool() {
    std::vector<std::uint8_t> memory(region_size, 0);
    auto creator = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{4096U, 60U}, {8192U, 40U}},
        BufferListRole::creator);
    auto allocation = creator ? creator.value.allocate(4096U)
                              : shmipc::shm::BufferAllocationResult{};
    auto active_header = shmipc::shm::read_buffer_list_header(
        memory.data() + shmipc::shm::buffer_manager_header_size,
        memory.size() - shmipc::shm::buffer_manager_header_size);
    if (!allocation || !active_header) {
        return false;
    }
    auto transient_header = active_header.value;
    transient_header.size = 0;
    if (shmipc::shm::write_buffer_list_header(
            memory.data() + shmipc::shm::buffer_manager_header_size,
            memory.size() - shmipc::shm::buffer_manager_header_size,
            transient_header) != shmipc::shm::BufferLayoutError::none) {
        return false;
    }
    auto mapper = shmipc::shm::map_buffer_pool(
        memory.data(), memory.size(), BufferListRole::mapper);
    if (shmipc::shm::write_buffer_list_header(
            memory.data() + shmipc::shm::buffer_manager_header_size,
            memory.size() - shmipc::shm::buffer_manager_header_size,
            active_header.value) != shmipc::shm::BufferLayoutError::none) {
        return false;
    }
    return creator && mapper &&
           creator.value.recycle(std::move(allocation.value)) ==
               BufferPoolError::none;
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

bool run_stress_worker(shmipc::shm::BufferPool& pool, std::uint8_t marker) {
    constexpr int iterations = 20000;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (;;) {
            auto allocation =
                pool.allocate(iteration % 3 == 0 ? 8192U : 4096U);
            if (!allocation) {
                if (allocation.error != BufferPoolError::no_buffer) {
                    std::cerr << "stress allocate failed marker="
                              << static_cast<int>(marker)
                              << " iteration=" << iteration << " error="
                              << shmipc::shm::to_string(allocation.error) << '\n';
                    return false;
                }
                static_cast<void>(::sched_yield());
                continue;
            }
            allocation.value.data()[0] = marker;
            allocation.value.data()[allocation.value.capacity() - 1U] = marker;
            const auto recycle_error = pool.recycle(std::move(allocation.value));
            if (recycle_error != BufferPoolError::none) {
                std::cerr << "stress recycle failed marker="
                          << static_cast<int>(marker)
                          << " iteration=" << iteration << " error="
                          << shmipc::shm::to_string(recycle_error) << '\n';
                return false;
            }
            break;
        }
    }
    return true;
}

bool test_cross_process_stress() {
    std::vector<char> path_template{
        '/', 't', 'm', 'p', '/', 's', 'h', 'm', 'i', 'p', 'c', '-', 'p', 'o',
        'o', 'l', '-', 's', 't', 'r', 'e', 's', 's', '.', 'X', 'X', 'X', 'X',
        'X', 'X', '\0'};
    const auto temporary_fd = ::mkstemp(path_template.data());
    if (temporary_fd < 0) {
        return false;
    }
    static_cast<void>(::close(temporary_fd));
    static_cast<void>(::unlink(path_template.data()));
    const std::string path(path_template.data());
    auto region = shmipc::shm::create_file_region(
        path, region_size, shmipc::shm::FileCleanup::unlink_on_destroy);
    if (!region) {
        return false;
    }
    auto creator = shmipc::shm::initialize_buffer_pool(
        region.value.data(), region.value.size(),
        {{4096U, 60U}, {8192U, 40U}}, BufferListRole::creator);
    if (!creator) {
        return false;
    }

    int ready_pipe[2]{};
    int start_pipe[2]{};
    if (::pipe(ready_pipe) != 0 || ::pipe(start_pipe) != 0) {
        return false;
    }
    const auto child = ::fork();
    if (child < 0) {
        return false;
    }
    if (child == 0) {
        static_cast<void>(::close(ready_pipe[0]));
        static_cast<void>(::close(start_pipe[1]));
        auto child_region = shmipc::shm::map_file_region(path);
        auto mapper = child_region
                          ? shmipc::shm::map_buffer_pool(
                                child_region.value.data(), child_region.value.size(),
                                BufferListRole::mapper)
                          : shmipc::shm::BufferPoolCreateResult{};
        const char ready = mapper ? 'R' : 'E';
        const auto ready_written = ::write(ready_pipe[1], &ready, 1U);
        char start = 0;
        const auto start_read = ::read(start_pipe[0], &start, 1U);
        const auto success = ready_written == 1 && start_read == 1 && start == 'S' &&
                             mapper && run_stress_worker(mapper.value, 0x5aU);
        ::_exit(success ? 0 : 1);
    }

    static_cast<void>(::close(ready_pipe[1]));
    static_cast<void>(::close(start_pipe[0]));
    char ready = 0;
    const auto ready_read = ::read(ready_pipe[0], &ready, 1U);
    const char start = 'S';
    const auto start_written = ::write(start_pipe[1], &start, 1U);
    const auto parent_success = ready_read == 1 && ready == 'R' &&
                                start_written == 1 &&
                                run_stress_worker(creator.value, 0xa5U);
    int child_status = 0;
    const auto waited = ::waitpid(child, &child_status, 0);
    static_cast<void>(::close(ready_pipe[0]));
    static_cast<void>(::close(start_pipe[1]));
    const auto mapped = shmipc::shm::map_buffer_pool(
        region.value.data(), region.value.size(), BufferListRole::mapper);
    const auto success = parent_success && waited == child &&
                         WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 &&
                         creator.value.all_returned() && mapped;
    if (!success) {
        std::cerr << "stress summary parent=" << parent_success
                  << " waited=" << (waited == child)
                  << " exited=" << WIFEXITED(child_status)
                  << " status=" << WEXITSTATUS(child_status)
                  << " returned=" << creator.value.all_returned()
                  << " map_error=" << shmipc::shm::to_string(mapped.error) << '\n';
    }
    return success;
}

bool transfer_chain(shmipc::shm::BufferPool& sender,
                    shmipc::shm::BufferPool& receiver,
                    std::uint8_t marker) {
    constexpr std::uint64_t payload_size = 20000U;
    auto chain = sender.allocate_chain(payload_size);
    if (!chain || chain.value.allocations.size() < 2U) {
        std::cerr << "chain allocate failed error="
                  << shmipc::shm::to_string(chain.error) << '\n';
        return false;
    }
    std::vector<std::uint32_t> sizes;
    sizes.reserve(chain.value.allocations.size());
    auto remaining = payload_size;
    for (auto& allocation : chain.value.allocations) {
        const auto size = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, allocation.capacity()));
        sizes.push_back(size);
        for (std::uint32_t index = 0; index < size; ++index) {
            allocation.data()[index] = marker;
        }
        remaining -= size;
    }
    auto published = sender.publish_chain(std::move(chain.value), sizes);
    if (!published || published.value.data_size != payload_size || chain.value) {
        std::cerr << "chain publish failed error="
                  << shmipc::shm::to_string(published.error) << '\n';
        return false;
    }
    auto adopted = receiver.adopt_chain(published.value.root_offset);
    if (!adopted || adopted.value.data_size != payload_size ||
        adopted.value.allocations.size() != published.value.slice_count) {
        std::cerr << "chain adopt failed error="
                  << shmipc::shm::to_string(adopted.error)
                  << " size=" << adopted.value.data_size << '\n';
        return false;
    }
    for (std::size_t index = 0; index < adopted.value.allocations.size(); ++index) {
        const auto& allocation = adopted.value.allocations[index];
        if (allocation.data()[0] != marker ||
            allocation.data()[sizes[index] - 1U] != marker) {
            std::cerr << "chain data mismatch\n";
            return false;
        }
    }
    const auto recycle_error = receiver.recycle_chain(std::move(adopted.value));
    if (recycle_error != BufferPoolError::none) {
        std::cerr << "chain recycle failed error="
                  << shmipc::shm::to_string(recycle_error) << '\n';
    }
    return recycle_error == BufferPoolError::none;
}

bool test_bidirectional_chains() {
    std::vector<std::uint8_t> memory(region_size, 0);
    auto creator = shmipc::shm::initialize_buffer_pool(
        memory.data(), memory.size(), {{4096U, 60U}, {8192U, 40U}},
        BufferListRole::creator);
    auto mapper = creator ? shmipc::shm::map_buffer_pool(
                                memory.data(), memory.size(),
                                BufferListRole::mapper)
                          : shmipc::shm::BufferPoolCreateResult{};
    if (!creator || !mapper ||
        !transfer_chain(creator.value, mapper.value, 0x3cU)) {
        return false;
    }
    if (creator.value.all_returned() || mapper.value.all_returned()) {
        return false;
    }
    return transfer_chain(mapper.value, creator.value, 0xc3U) &&
           creator.value.all_returned() && mapper.value.all_returned() &&
           shmipc::shm::map_buffer_pool(memory.data(), memory.size(),
                                        BufferListRole::mapper);
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
    if (!test_map_live_pool()) {
        std::cerr << "live buffer pool mapping test failed\n";
        return 1;
    }
    if (!test_wrong_pool_and_corrupt_manager()) {
        std::cerr << "buffer pool invalid ownership/layout test failed\n";
        return 1;
    }
    if (!test_cross_process_stress()) {
        std::cerr << "buffer pool cross-process stress test failed\n";
        return 1;
    }
    if (!test_bidirectional_chains()) {
        std::cerr << "buffer pool bidirectional chain test failed\n";
        return 1;
    }
    return 0;
}
