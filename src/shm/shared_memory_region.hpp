#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace shmipc::shm {

enum class SharedMemoryKind {
    none,
    file,
    memfd,
};

enum class FileCleanup {
    keep,
    unlink_on_destroy,
};

enum class FdOwnership {
    borrowed,
    transferred,
};

enum class MappingError {
    none,
    invalid_argument,
    unsupported,
    open_failed,
    duplicate_failed,
    stat_failed,
    resize_failed,
    map_failed,
};

struct MappingStatus {
    MappingError error{MappingError::none};
    int system_error{0};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == MappingError::none;
    }
};

template <typename T>
struct MappingResult {
    T value{};
    MappingStatus status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

class SharedMemoryRegion final {
public:
    SharedMemoryRegion() noexcept = default;
    ~SharedMemoryRegion() noexcept;

    SharedMemoryRegion(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion(SharedMemoryRegion&& other) noexcept;
    SharedMemoryRegion& operator=(SharedMemoryRegion&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint8_t* data() noexcept;
    [[nodiscard]] const std::uint8_t* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] int fd() const noexcept;
    [[nodiscard]] SharedMemoryKind kind() const noexcept;
    [[nodiscard]] const std::string& path() const noexcept;

    void reset() noexcept;

private:
    friend MappingResult<SharedMemoryRegion> create_file_region(
        const std::string&, std::size_t, FileCleanup);
    friend MappingResult<SharedMemoryRegion> map_file_region(
        const std::string&);
    friend MappingResult<SharedMemoryRegion> create_memfd_region(
        const std::string&, std::size_t);
    friend MappingResult<SharedMemoryRegion> map_memfd_region(
        int, FdOwnership) noexcept;

    SharedMemoryRegion(void* address, std::size_t size, int fd,
                       SharedMemoryKind kind, std::string path,
                       FileCleanup cleanup) noexcept;

    void* address_{nullptr};
    std::size_t size_{0};
    int fd_{-1};
    SharedMemoryKind kind_{SharedMemoryKind::none};
    std::string path_{};
    FileCleanup cleanup_{FileCleanup::keep};
};

using SharedMemoryResult = MappingResult<SharedMemoryRegion>;

[[nodiscard]] const char* to_string(SharedMemoryKind kind) noexcept;
[[nodiscard]] const char* to_string(MappingError error) noexcept;

[[nodiscard]] SharedMemoryResult create_file_region(
    const std::string& path, std::size_t size,
    FileCleanup cleanup = FileCleanup::unlink_on_destroy);
[[nodiscard]] SharedMemoryResult map_file_region(
    const std::string& path);
[[nodiscard]] SharedMemoryResult create_memfd_region(
    const std::string& name, std::size_t size);
[[nodiscard]] SharedMemoryResult map_memfd_region(
    int fd, FdOwnership ownership) noexcept;

}  // namespace shmipc::shm
