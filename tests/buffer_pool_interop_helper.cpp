#include "shm/buffer_io.hpp"
#include "shm/shared_memory_region.hpp"

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
    if (!pool) {
        return false;
    }
    std::vector<std::uint8_t> payload(payload_size, cpp_marker);
    shmipc::shm::BufferWriter writer(pool.value);
    const auto written = writer.write_bytes(payload.data(), payload.size());
    auto published = written ? writer.publish()
                             : shmipc::shm::PublishedBufferChainResult{};
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
    auto reader = chain ? shmipc::shm::make_buffer_reader(
                              pool.value, std::move(chain.value))
                        : shmipc::shm::BufferReaderResult{};
    if (!region || !pool || !chain || !reader ||
        reader.value.remaining() != payload_size) {
        std::cerr << "setup region=" << static_cast<bool>(region)
                  << " pool=" << static_cast<bool>(pool)
                  << " chain=" << static_cast<bool>(chain)
                  << " reader=" << static_cast<bool>(reader)
                  << " remaining=" << reader.value.remaining() << '\n';
        return false;
    }
    const auto payload = reader.value.read_bytes(payload_size);
    if (!payload || payload.value.is_zero_copy()) {
        std::cerr << "read error=" << shmipc::shm::to_string(payload.error)
                  << " zero_copy=" << payload.value.is_zero_copy() << '\n';
        return false;
    }
    for (std::size_t index = 0; index < payload.value.size(); ++index) {
        if (payload.value.data()[index] != go_marker) {
            std::cerr << "marker mismatch at " << index << '\n';
            return false;
        }
    }
    const auto release = reader.value.release_previous_read();
    const auto returned = pool.value.all_returned();
    const auto remapped = shmipc::shm::map_buffer_pool(
        region.value.data(), region.value.size(),
        shmipc::shm::BufferListRole::mapper);
    if (release != shmipc::shm::BufferIoError::none || !returned || !remapped) {
        std::cerr << "release=" << shmipc::shm::to_string(release)
                  << " returned=" << returned
                  << " remap=" << static_cast<bool>(remapped) << '\n';
        return false;
    }
    return true;
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
