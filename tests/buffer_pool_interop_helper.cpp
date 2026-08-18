#include "shm/buffer_pool.hpp"
#include "shm/shared_memory_region.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t region_size = 1U << 20U;
constexpr std::uint64_t payload_size = 20000U;
constexpr std::uint8_t cpp_marker = 0x3cU;
constexpr std::uint8_t go_marker = 0x6bU;

bool create_for_go(const std::string& path) {
    auto region = shmipc::shm::create_file_region(
        path, region_size, shmipc::shm::FileCleanup::keep);
    if (!region) {
        return false;
    }
    auto pool = shmipc::shm::initialize_buffer_pool(
        region.value.data(), region.value.size(),
        {{4096U, 60U}, {8192U, 40U}}, shmipc::shm::BufferListRole::creator);
    auto chain = pool ? pool.value.allocate_chain(payload_size)
                      : shmipc::shm::BufferChainResult{};
    if (!pool || !chain) {
        return false;
    }
    std::vector<std::uint32_t> sizes;
    auto remaining = payload_size;
    for (auto& allocation : chain.value.allocations) {
        const auto size = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, allocation.capacity()));
        sizes.push_back(size);
        std::fill_n(allocation.data(), size, cpp_marker);
        remaining -= size;
    }
    auto published = pool.value.publish_chain(std::move(chain.value), sizes);
    if (!published || published.value.data_size != payload_size) {
        return false;
    }
    std::cout << published.value.root_offset << '\n';
    return static_cast<bool>(std::cout);
}

bool verify_from_go(const std::string& path, std::uint32_t root_offset) {
    auto region = shmipc::shm::map_file_region(path);
    auto pool = region ? shmipc::shm::map_buffer_pool(
                             region.value.data(), region.value.size(),
                             shmipc::shm::BufferListRole::creator)
                       : shmipc::shm::BufferPoolCreateResult{};
    auto chain = pool ? pool.value.adopt_chain(root_offset)
                      : shmipc::shm::BufferChainResult{};
    if (!region || !pool || !chain || chain.value.data_size != payload_size) {
        return false;
    }
    std::uint64_t remaining = payload_size;
    for (const auto& allocation : chain.value.allocations) {
        const auto size = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, allocation.capacity()));
        if (allocation.data()[0] != go_marker ||
            allocation.data()[size - 1U] != go_marker) {
            return false;
        }
        remaining -= size;
    }
    return pool.value.recycle_chain(std::move(chain.value)) ==
               shmipc::shm::BufferPoolError::none &&
           pool.value.all_returned() &&
           shmipc::shm::map_buffer_pool(region.value.data(), region.value.size(),
                                        shmipc::shm::BufferListRole::mapper);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "create") {
        return create_for_go(argv[2]) ? 0 : 1;
    }
    if (argc == 4 && std::string(argv[1]) == "verify") {
        char* end = nullptr;
        const auto value = std::strtoul(argv[3], &end, 10);
        if (end == argv[3] || *end != '\0' || value > UINT32_MAX) {
            return 2;
        }
        return verify_from_go(argv[2], static_cast<std::uint32_t>(value)) ? 0 : 1;
    }
    std::cerr << "usage: buffer_pool_interop_helper create <path> | verify <path> <root>\n";
    return 2;
}
