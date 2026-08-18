#include "shm/shared_memory_region.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace {

using shmipc::shm::FdOwnership;
using shmipc::shm::FileCleanup;
using shmipc::shm::MappingError;
using shmipc::shm::SharedMemoryKind;

bool test_invalid_arguments() {
    const auto empty_path = shmipc::shm::create_file_region("", 4096U);
    const auto zero_size = shmipc::shm::create_file_region("ignored", 0U);
    const auto missing = shmipc::shm::map_file_region(
        "/path/that/does/not/exist/shmipc");
    const auto invalid_fd =
        shmipc::shm::map_memfd_region(-1, FdOwnership::borrowed);
    return empty_path.status.error == MappingError::invalid_argument &&
           zero_size.status.error == MappingError::invalid_argument &&
           missing.status.error == MappingError::open_failed &&
           missing.status.system_error != 0 &&
           invalid_fd.status.error == MappingError::invalid_argument;
}

bool test_file_mapping() {
    std::array<char, 64> directory_template{};
    const std::string pattern = "/tmp/shmipc-region-test.XXXXXX";
    std::copy(pattern.begin(), pattern.end(), directory_template.begin());
    auto* const directory = ::mkdtemp(directory_template.data());
    if (directory == nullptr) {
        return false;
    }
    const std::string path = std::string(directory) + "/region";

    auto creator = shmipc::shm::create_file_region(
        path, 4096U, FileCleanup::unlink_on_destroy);
    if (!creator || creator.value.kind() != SharedMemoryKind::file ||
        creator.value.size() != 4096U || creator.value.fd() != -1 ||
        creator.value.path() != path || ::access(path.c_str(), F_OK) != 0) {
        static_cast<void>(::rmdir(directory));
        return false;
    }

    const auto duplicate = shmipc::shm::create_file_region(path, 4096U);
    if (duplicate.status.error != MappingError::open_failed ||
        duplicate.status.system_error != EEXIST) {
        return false;
    }

    creator.value.data()[17] = 0x5aU;
    auto mapper = shmipc::shm::map_file_region(path);
    if (!mapper || mapper.value.data()[17] != 0x5aU ||
        mapper.value.path() != path) {
        return false;
    }
    mapper.value.data()[18] = 0xa5U;
    if (creator.value.data()[18] != 0xa5U) {
        return false;
    }

    auto moved = std::move(creator.value);
    if (creator.value || !moved || moved.data()[18] != 0xa5U) {
        return false;
    }
    moved.reset();
    if (::access(path.c_str(), F_OK) == 0 || mapper.value.data()[17] != 0x5aU) {
        return false;
    }
    mapper.value.reset();
    return ::rmdir(directory) == 0;
}

bool test_memfd_mapping() {
    auto creator = shmipc::shm::create_memfd_region("shmipc-test", 4096U);
#if defined(__linux__)
    if (!creator || creator.value.kind() != SharedMemoryKind::memfd ||
        creator.value.size() != 4096U || creator.value.fd() < 0 ||
        !creator.value.path().empty()) {
        return false;
    }
    creator.value.data()[31] = 0x7cU;

    const auto original_fd = creator.value.fd();
    auto borrowed =
        shmipc::shm::map_memfd_region(original_fd, FdOwnership::borrowed);
    if (!borrowed || borrowed.value.fd() == original_fd ||
        borrowed.value.data()[31] != 0x7cU) {
        return false;
    }
    borrowed.value.reset();
    if (::fcntl(original_fd, F_GETFD) < 0) {
        return false;
    }

    const auto transferred_fd = ::dup(original_fd);
    if (transferred_fd < 0) {
        return false;
    }
    auto transferred = shmipc::shm::map_memfd_region(
        transferred_fd, FdOwnership::transferred);
    if (!transferred || transferred.value.fd() != transferred_fd) {
        return false;
    }
    transferred.value.reset();
    errno = 0;
    return ::fcntl(transferred_fd, F_GETFD) == -1 && errno == EBADF;
#else
    return creator.status.error == MappingError::unsupported;
#endif
}

}  // namespace

int main() {
    if (!test_invalid_arguments()) {
        std::cerr << "shared memory invalid-argument test failed\n";
        return 1;
    }
    if (!test_file_mapping()) {
        std::cerr << "shared memory file mapping test failed\n";
        return 1;
    }
    if (!test_memfd_mapping()) {
        std::cerr << "shared memory memfd mapping test failed\n";
        return 1;
    }
    return 0;
}
