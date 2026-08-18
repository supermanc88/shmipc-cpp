#include "shm/shared_memory_region.hpp"
#include "shm/shared_queue.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr std::uint32_t queue_capacity = 2048U;
constexpr std::uint32_t element_count = 1000U;

bool create_for_go(const std::string& path) {
    const auto manager_size = shmipc::shm::queue_manager_region_size(
        queue_capacity, shmipc::shm::native_queue_architecture());
    if (!manager_size) {
        return false;
    }
    auto region = shmipc::shm::create_file_region(
        path, manager_size.value, shmipc::shm::FileCleanup::keep);
    if (!region) {
        return false;
    }
    const auto queue_size = manager_size.value / shmipc::shm::queue_count;
    auto outbound = shmipc::shm::initialize_shared_queue(
        region.value.data(), queue_size, queue_capacity);
    auto inbound = shmipc::shm::initialize_shared_queue(
        region.value.data() + queue_size, queue_size, queue_capacity);
    if (!outbound || !inbound) {
        return false;
    }
    for (std::uint32_t value = 0; value < element_count; ++value) {
        if (outbound.value.put({value, value + 7U, value ^ 0x55aaU}) !=
            shmipc::shm::QueueError::none) {
            return false;
        }
    }
    return outbound.value.mark_working();
}

bool verify_from_go(const std::string& path) {
    auto region = shmipc::shm::map_file_region(path);
    if (!region || region.value.size() % shmipc::shm::queue_count != 0U) {
        return false;
    }
    const auto queue_size = region.value.size() / shmipc::shm::queue_count;
    auto inbound = shmipc::shm::map_shared_queue(
        region.value.data() + queue_size, queue_size);
    if (!inbound || !inbound.value.consumer_is_working()) {
        return false;
    }
    const auto elements = inbound.value.pop_batch(element_count + 1U);
    if (!elements || elements.value.size() != element_count) {
        return false;
    }
    for (std::uint32_t value = 0; value < element_count; ++value) {
        const auto& element = elements.value[value];
        if (element.sequence_id != value + 10000U ||
            element.buffer_offset != value + 17U ||
            element.status != (value ^ 0xaa55U)) {
            return false;
        }
    }
    return inbound.value.mark_not_working() &&
           !inbound.value.consumer_is_working();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "create") {
        return create_for_go(argv[2]) ? 0 : 1;
    }
    if (argc == 3 && std::string(argv[1]) == "verify") {
        return verify_from_go(argv[2]) ? 0 : 1;
    }
    std::cerr << "usage: shared_queue_interop_helper create|verify <path>\n";
    return 2;
}
