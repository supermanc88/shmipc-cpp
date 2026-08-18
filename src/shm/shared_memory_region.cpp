#include "shm/shared_memory_region.hpp"

#include <cerrno>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/memfd.h>
#include <sys/syscall.h>
#endif

namespace shmipc::shm {
namespace {

class UniqueFd final {
public:
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}
    ~UniqueFd() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                static_cast<void>(::close(fd_));
            }
            fd_ = other.release();
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] int release() noexcept {
        const auto fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

MappingStatus error_status(MappingError error) noexcept {
    return {error, errno};
}

bool size_fits_off_t(std::size_t size) noexcept {
    return size > 0U && static_cast<std::uintmax_t>(size) <=
                            static_cast<std::uintmax_t>(
                                std::numeric_limits<off_t>::max());
}

MappingStatus mapped_size(int fd, std::size_t& size) noexcept {
    struct stat details {};
    if (::fstat(fd, &details) != 0) {
        return error_status(MappingError::stat_failed);
    }
    if (details.st_size <= 0) {
        return {MappingError::invalid_argument, 0};
    }
    const auto unsigned_size = static_cast<std::uintmax_t>(details.st_size);
    if (unsigned_size > std::numeric_limits<std::size_t>::max()) {
        return {MappingError::invalid_argument, 0};
    }
    size = static_cast<std::size_t>(unsigned_size);
    return {};
}

void* map_shared(int fd, std::size_t size) noexcept {
    return ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

int duplicate_fd(int fd) noexcept {
#if defined(F_DUPFD_CLOEXEC)
    return ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
    const auto duplicate = ::dup(fd);
    if (duplicate >= 0 && ::fcntl(duplicate, F_SETFD, FD_CLOEXEC) != 0) {
        const auto saved_errno = errno;
        static_cast<void>(::close(duplicate));
        errno = saved_errno;
        return -1;
    }
    return duplicate;
#endif
}

}  // namespace

SharedMemoryRegion::SharedMemoryRegion(void* address, std::size_t size, int fd,
                                       SharedMemoryKind kind, std::string path,
                                       FileCleanup cleanup) noexcept
    : address_(address),
      size_(size),
      fd_(fd),
      kind_(kind),
      path_(std::move(path)),
      cleanup_(cleanup) {}

SharedMemoryRegion::~SharedMemoryRegion() noexcept { reset(); }

SharedMemoryRegion::SharedMemoryRegion(SharedMemoryRegion&& other) noexcept
    : address_(other.address_),
      size_(other.size_),
      fd_(other.fd_),
      kind_(other.kind_),
      path_(std::move(other.path_)),
      cleanup_(other.cleanup_) {
    other.address_ = nullptr;
    other.size_ = 0;
    other.fd_ = -1;
    other.kind_ = SharedMemoryKind::none;
    other.cleanup_ = FileCleanup::keep;
}

SharedMemoryRegion& SharedMemoryRegion::operator=(
    SharedMemoryRegion&& other) noexcept {
    if (this != &other) {
        reset();
        address_ = other.address_;
        size_ = other.size_;
        fd_ = other.fd_;
        kind_ = other.kind_;
        path_ = std::move(other.path_);
        cleanup_ = other.cleanup_;
        other.address_ = nullptr;
        other.size_ = 0;
        other.fd_ = -1;
        other.kind_ = SharedMemoryKind::none;
        other.cleanup_ = FileCleanup::keep;
    }
    return *this;
}

SharedMemoryRegion::operator bool() const noexcept { return address_ != nullptr; }

std::uint8_t* SharedMemoryRegion::data() noexcept {
    return static_cast<std::uint8_t*>(address_);
}

const std::uint8_t* SharedMemoryRegion::data() const noexcept {
    return static_cast<const std::uint8_t*>(address_);
}

std::size_t SharedMemoryRegion::size() const noexcept { return size_; }

int SharedMemoryRegion::fd() const noexcept { return fd_; }

SharedMemoryKind SharedMemoryRegion::kind() const noexcept { return kind_; }

const std::string& SharedMemoryRegion::path() const noexcept { return path_; }

void SharedMemoryRegion::reset() noexcept {
    if (address_ != nullptr) {
        static_cast<void>(::munmap(address_, size_));
    }
    if (fd_ >= 0) {
        static_cast<void>(::close(fd_));
    }
    if (cleanup_ == FileCleanup::unlink_on_destroy && !path_.empty()) {
        static_cast<void>(::unlink(path_.c_str()));
    }
    address_ = nullptr;
    size_ = 0;
    fd_ = -1;
    kind_ = SharedMemoryKind::none;
    path_.clear();
    cleanup_ = FileCleanup::keep;
}

const char* to_string(SharedMemoryKind kind) noexcept {
    switch (kind) {
        case SharedMemoryKind::none:
            return "none";
        case SharedMemoryKind::file:
            return "file";
        case SharedMemoryKind::memfd:
            return "memfd";
    }
    return "unknown shared-memory kind";
}

const char* to_string(MappingError error) noexcept {
    switch (error) {
        case MappingError::none:
            return "none";
        case MappingError::invalid_argument:
            return "invalid argument";
        case MappingError::unsupported:
            return "unsupported";
        case MappingError::open_failed:
            return "open failed";
        case MappingError::duplicate_failed:
            return "descriptor duplication failed";
        case MappingError::stat_failed:
            return "stat failed";
        case MappingError::resize_failed:
            return "resize failed";
        case MappingError::map_failed:
            return "map failed";
    }
    return "unknown mapping error";
}

SharedMemoryResult create_file_region(const std::string& path, std::size_t size,
                                      FileCleanup cleanup) {
    if (path.empty() || !size_fits_off_t(size)) {
        return {{}, {MappingError::invalid_argument, 0}};
    }
    UniqueFd fd(::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600));
    if (fd.get() < 0) {
        return {{}, error_status(MappingError::open_failed)};
    }
    if (::ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
        const auto status = error_status(MappingError::resize_failed);
        static_cast<void>(::unlink(path.c_str()));
        return {{}, status};
    }
    auto* const address = map_shared(fd.get(), size);
    if (address == MAP_FAILED) {
        const auto status = error_status(MappingError::map_failed);
        static_cast<void>(::unlink(path.c_str()));
        return {{}, status};
    }
    return {SharedMemoryRegion(address, size, -1, SharedMemoryKind::file, path,
                               cleanup),
            {}};
}

SharedMemoryResult map_file_region(const std::string& path) {
    if (path.empty()) {
        return {{}, {MappingError::invalid_argument, 0}};
    }
    UniqueFd fd(::open(path.c_str(), O_RDWR | O_CLOEXEC));
    if (fd.get() < 0) {
        return {{}, error_status(MappingError::open_failed)};
    }
    std::size_t size = 0;
    const auto status = mapped_size(fd.get(), size);
    if (!status) {
        return {{}, status};
    }
    auto* const address = map_shared(fd.get(), size);
    if (address == MAP_FAILED) {
        return {{}, error_status(MappingError::map_failed)};
    }
    return {SharedMemoryRegion(address, size, -1, SharedMemoryKind::file, path,
                               FileCleanup::keep),
            {}};
}

SharedMemoryResult create_memfd_region(const std::string& name,
                                       std::size_t size) {
    if (name.empty() || !size_fits_off_t(size)) {
        return {{}, {MappingError::invalid_argument, 0}};
    }
#if defined(__linux__) && defined(SYS_memfd_create)
    const auto raw_fd = ::syscall(SYS_memfd_create, name.c_str(), MFD_CLOEXEC);
    if (raw_fd < 0) {
        return {{}, error_status(MappingError::open_failed)};
    }
    UniqueFd fd(static_cast<int>(raw_fd));
    if (::ftruncate(fd.get(), static_cast<off_t>(size)) != 0) {
        return {{}, error_status(MappingError::resize_failed)};
    }
    auto* const address = map_shared(fd.get(), size);
    if (address == MAP_FAILED) {
        return {{}, error_status(MappingError::map_failed)};
    }
    return {SharedMemoryRegion(address, size, fd.release(),
                               SharedMemoryKind::memfd, {}, FileCleanup::keep),
            {}};
#else
    static_cast<void>(size);
    return {{}, {MappingError::unsupported, 0}};
#endif
}

SharedMemoryResult map_memfd_region(int fd, FdOwnership ownership) noexcept {
    if (fd < 0) {
        return {{}, {MappingError::invalid_argument, 0}};
    }
    UniqueFd owned;
    if (ownership == FdOwnership::transferred) {
        owned = UniqueFd(fd);
    } else {
        UniqueFd duplicate(duplicate_fd(fd));
        if (duplicate.get() < 0) {
            return {{}, error_status(MappingError::duplicate_failed)};
        }
        owned = std::move(duplicate);
    }
    std::size_t size = 0;
    const auto status = mapped_size(owned.get(), size);
    if (!status) {
        return {{}, status};
    }
    auto* const address = map_shared(owned.get(), size);
    if (address == MAP_FAILED) {
        return {{}, error_status(MappingError::map_failed)};
    }
    return {SharedMemoryRegion(address, size, owned.release(),
                               SharedMemoryKind::memfd, {}, FileCleanup::keep),
            {}};
}

}  // namespace shmipc::shm
