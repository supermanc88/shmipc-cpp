#include "transport/control_socket.hpp"

#include <cerrno>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace shmipc::transport {
namespace {

TransportError close_fd(int& fd) noexcept {
    if (fd < 0) {
        return TransportError::none;
    }
    const auto owned = fd;
    fd = -1;
    if (::close(owned) != 0) {
        return TransportError::system_error;
    }
    return TransportError::none;
}

bool configure_descriptor(int fd) noexcept {
    const auto flags = ::fcntl(fd, F_GETFD);
    if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        return false;
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
                     sizeof(enabled)) != 0) {
        return false;
    }
#endif
    return true;
}

bool valid_unix_path(const std::string& path) noexcept {
    sockaddr_un address{};
    return !path.empty() && path.size() < sizeof(address.sun_path);
}

socklen_t unix_address(const std::string& path, sockaddr_un& address) noexcept {
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
    return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                  path.size() + 1U);
}

int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

constexpr std::size_t maximum_descriptor_transfer_count = 16U;

#if defined(__linux__)
TransportError descriptor_error(int error) noexcept {
    return error == EAGAIN || error == EWOULDBLOCK
               ? TransportError::would_block
               : TransportError::system_error;
}
#endif

}  // namespace

ReceivedFileDescriptors::ReceivedFileDescriptors(
    std::vector<int>&& descriptors) noexcept
    : descriptors_(std::move(descriptors)) {}

ReceivedFileDescriptors::~ReceivedFileDescriptors() noexcept { reset(); }

ReceivedFileDescriptors::ReceivedFileDescriptors(
    ReceivedFileDescriptors&& other) noexcept
    : descriptors_(std::move(other.descriptors_)) {
    other.descriptors_.clear();
}

ReceivedFileDescriptors&
ReceivedFileDescriptors::operator=(ReceivedFileDescriptors&& other) noexcept {
    if (this != &other) {
        reset();
        descriptors_ = std::move(other.descriptors_);
        other.descriptors_.clear();
    }
    return *this;
}

std::size_t ReceivedFileDescriptors::size() const noexcept {
    return descriptors_.size();
}

int ReceivedFileDescriptors::at(std::size_t index) const noexcept {
    return index < descriptors_.size() ? descriptors_[index] : -1;
}

int ReceivedFileDescriptors::release(std::size_t index) noexcept {
    if (index >= descriptors_.size()) {
        return -1;
    }
    const auto descriptor = descriptors_[index];
    descriptors_[index] = -1;
    return descriptor;
}

void ReceivedFileDescriptors::reset() noexcept {
    for (auto& descriptor : descriptors_) {
        static_cast<void>(close_fd(descriptor));
    }
    descriptors_.clear();
}

ControlSocket::ControlSocket(int fd) noexcept : fd_(fd) {}

ControlSocket::~ControlSocket() { static_cast<void>(close()); }

ControlSocket::ControlSocket(ControlSocket&& other) noexcept
    : fd_(other.release()) {}

ControlSocket& ControlSocket::operator=(ControlSocket&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        fd_ = other.release();
    }
    return *this;
}

ControlSocket::operator bool() const noexcept { return fd_ >= 0; }

int ControlSocket::native_handle() const noexcept { return fd_; }

int ControlSocket::release() noexcept {
    const auto result = fd_;
    fd_ = -1;
    return result;
}

TransportError ControlSocket::close() noexcept { return close_fd(fd_); }

TransportError ControlSocket::shutdown() noexcept {
    if (fd_ < 0) {
        return TransportError::invalid_state;
    }
    if (::shutdown(fd_, SHUT_RDWR) != 0 && errno != ENOTCONN) {
        return TransportError::system_error;
    }
    return TransportError::none;
}

TransportError ControlSocket::set_nonblocking(bool enabled) noexcept {
    if (fd_ < 0) {
        return TransportError::invalid_state;
    }
    const auto flags = ::fcntl(fd_, F_GETFL);
    if (flags < 0) {
        return TransportError::system_error;
    }
    const auto desired = enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    if (::fcntl(fd_, F_SETFL, desired) != 0) {
        return TransportError::system_error;
    }
    return TransportError::none;
}

IoResult ControlSocket::read_full(std::uint8_t* data,
                                  std::size_t size) noexcept {
    if (fd_ < 0) {
        return {{0U}, TransportError::invalid_state, EBADF};
    }
    if (data == nullptr && size != 0U) {
        return {{0U}, TransportError::invalid_argument, EINVAL};
    }
    std::size_t transferred = 0;
    while (transferred < size) {
        const auto count = ::read(fd_, data + transferred, size - transferred);
        if (count > 0) {
            transferred += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0) {
            return {{transferred}, TransportError::end_of_stream, 0};
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return {{transferred}, TransportError::would_block, errno};
        }
        return {{transferred}, TransportError::system_error, errno};
    }
    return {{transferred}, TransportError::none, 0};
}

IoResult ControlSocket::write_full(const std::uint8_t* data,
                                   std::size_t size) noexcept {
    if (fd_ < 0) {
        return {{0U}, TransportError::invalid_state, EBADF};
    }
    if (data == nullptr && size != 0U) {
        return {{0U}, TransportError::invalid_argument, EINVAL};
    }
    std::size_t transferred = 0;
    while (transferred < size) {
        const auto count = ::send(fd_, data + transferred, size - transferred,
                                  send_flags());
        if (count > 0) {
            transferred += static_cast<std::size_t>(count);
            continue;
        }
        if (count == 0) {
            return {{transferred}, TransportError::system_error, EIO};
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return {{transferred}, TransportError::would_block, errno};
        }
        return {{transferred}, TransportError::system_error, errno};
    }
    return {{transferred}, TransportError::none, 0};
}

IoResult ControlSocket::send_file_descriptors(const int* descriptors,
                                              std::size_t count) noexcept {
    if (fd_ < 0) {
        return {{0U}, TransportError::invalid_state, EBADF};
    }
    if (descriptors == nullptr || count == 0U ||
        count > maximum_descriptor_transfer_count ||
        count > std::numeric_limits<unsigned int>::max() / sizeof(int)) {
        return {{0U}, TransportError::invalid_argument, EINVAL};
    }
#if defined(__linux__)
    for (std::size_t index = 0; index < count; ++index) {
        if (descriptors[index] < 0) {
            return {{0U}, TransportError::invalid_argument, EINVAL};
        }
    }
    std::vector<std::uint8_t> control(CMSG_SPACE(count * sizeof(int)), 0U);
    std::uint8_t payload = 0U;
    iovec payload_vector{};
    payload_vector.iov_base = &payload;
    payload_vector.iov_len = sizeof(payload);
    msghdr message{};
    message.msg_iov = &payload_vector;
    message.msg_iovlen = 1U;
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    auto* const header = CMSG_FIRSTHDR(&message);
    if (header == nullptr) {
        return {{0U}, TransportError::system_error, EIO};
    }
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(count * sizeof(int));
    std::memcpy(CMSG_DATA(header), descriptors, count * sizeof(int));

    ssize_t sent = -1;
    do {
        sent = ::sendmsg(fd_, &message, send_flags());
    } while (sent < 0 && errno == EINTR);
    if (sent < 0) {
        const auto saved = errno;
        return {{0U}, descriptor_error(saved), saved};
    }
    if (sent != 1) {
        return {{0U}, TransportError::system_error, EIO};
    }
    return {{count}, TransportError::none, 0};
#else
    static_cast<void>(descriptors);
    static_cast<void>(count);
    return {{0U}, TransportError::unsupported, ENOTSUP};
#endif
}

FileDescriptorResult
ControlSocket::receive_file_descriptors(std::size_t maximum_count) noexcept {
    if (fd_ < 0) {
        return {{}, TransportError::invalid_state, EBADF};
    }
    if (maximum_count == 0U ||
        maximum_count > maximum_descriptor_transfer_count ||
        maximum_count >
            std::numeric_limits<unsigned int>::max() / sizeof(int)) {
        return {{}, TransportError::invalid_argument, EINVAL};
    }
#if defined(__linux__)
    std::vector<std::uint8_t> control(CMSG_SPACE(maximum_count * sizeof(int)),
                                      0U);
    std::uint8_t payload = 0xffU;
    iovec payload_vector{};
    payload_vector.iov_base = &payload;
    payload_vector.iov_len = sizeof(payload);
    msghdr message{};
    message.msg_iov = &payload_vector;
    message.msg_iovlen = 1U;
    message.msg_control = control.data();
    message.msg_controllen = control.size();

    ssize_t received_count = -1;
    do {
#ifdef MSG_CMSG_CLOEXEC
        received_count = ::recvmsg(fd_, &message, MSG_CMSG_CLOEXEC);
#else
        received_count = ::recvmsg(fd_, &message, 0);
#endif
    } while (received_count < 0 && errno == EINTR);
    if (received_count < 0) {
        const auto saved = errno;
        return {{}, descriptor_error(saved), saved};
    }

    std::vector<int> descriptors;
    bool invalid_control_message = false;
    for (auto* header = CMSG_FIRSTHDR(&message); header != nullptr;
         header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET ||
            header->cmsg_type != SCM_RIGHTS) {
            invalid_control_message = true;
            continue;
        }
        if (header->cmsg_len < CMSG_LEN(0U)) {
            invalid_control_message = true;
            continue;
        }
        const auto bytes = header->cmsg_len - CMSG_LEN(0U);
        if (bytes == 0U || bytes % sizeof(int) != 0U) {
            invalid_control_message = true;
            continue;
        }
        const auto descriptor_count = bytes / sizeof(int);
        const auto* received = reinterpret_cast<const int*>(CMSG_DATA(header));
        descriptors.insert(descriptors.end(), received,
                           received + descriptor_count);
    }
    ReceivedFileDescriptors result(std::move(descriptors));
    if ((message.msg_flags & MSG_CTRUNC) != 0 ||
        result.size() > maximum_count) {
        return {std::move(result), TransportError::buffer_limit, EMSGSIZE};
    }
    if (invalid_control_message) {
        return {std::move(result), TransportError::invalid_argument, EPROTO};
    }
    if (result.size() == 0U) {
        return {std::move(result),
                received_count == 0 ? TransportError::end_of_stream
                                    : TransportError::invalid_argument,
                received_count == 0 ? 0 : EPROTO};
    }
    if (received_count != 1 || payload != 0U) {
        return {std::move(result), TransportError::invalid_argument, EPROTO};
    }
#ifndef MSG_CMSG_CLOEXEC
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto descriptor = result.at(index);
        const auto flags = ::fcntl(descriptor, F_GETFD);
        if (flags < 0 ||
            ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
            const auto saved = errno;
            return {std::move(result), TransportError::system_error, saved};
        }
    }
#endif
    return {std::move(result), TransportError::none, 0};
#else
    static_cast<void>(maximum_count);
    return {{}, TransportError::unsupported, ENOTSUP};
#endif
}

ControlListener::ControlListener(int fd, std::string unix_path) noexcept
    : fd_(fd), unix_path_(std::move(unix_path)) {}

ControlListener::~ControlListener() { static_cast<void>(close()); }

ControlListener::ControlListener(ControlListener&& other) noexcept
    : fd_(other.fd_), unix_path_(std::move(other.unix_path_)) {
    other.fd_ = -1;
    other.unix_path_.clear();
}

ControlListener& ControlListener::operator=(ControlListener&& other) noexcept {
    if (this != &other) {
        static_cast<void>(close());
        fd_ = other.fd_;
        unix_path_ = std::move(other.unix_path_);
        other.fd_ = -1;
        other.unix_path_.clear();
    }
    return *this;
}

ControlListener::operator bool() const noexcept { return fd_ >= 0; }

int ControlListener::native_handle() const noexcept { return fd_; }

ControlSocketResult ControlListener::accept() noexcept {
    if (fd_ < 0) {
        return {{}, TransportError::invalid_state, EBADF};
    }
    int accepted = -1;
    do {
        accepted = ::accept(fd_, nullptr, nullptr);
    } while (accepted < 0 && errno == EINTR);
    if (accepted < 0) {
        const auto saved = errno;
        return {{}, (saved == EAGAIN || saved == EWOULDBLOCK)
                        ? TransportError::would_block
                        : TransportError::system_error,
                saved};
    }
    return adopt_control_socket(accepted);
}

TransportResult<std::uint16_t> ControlListener::local_port() const noexcept {
    if (fd_ < 0) {
        return {0U, TransportError::invalid_state, EBADF};
    }
    sockaddr_storage address{};
    socklen_t size = sizeof(address);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
        return {0U, TransportError::system_error, errno};
    }
    if (address.ss_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        return {ntohs(ipv4->sin_port), TransportError::none, 0};
    }
    if (address.ss_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        return {ntohs(ipv6->sin6_port), TransportError::none, 0};
    }
    return {0U, TransportError::invalid_state, EAFNOSUPPORT};
}

TransportError ControlListener::close() noexcept {
    const auto result = close_fd(fd_);
    if (!unix_path_.empty()) {
        static_cast<void>(::unlink(unix_path_.c_str()));
        unix_path_.clear();
    }
    return result;
}

const char* to_string(TransportError error) noexcept {
    switch (error) {
        case TransportError::none:
            return "none";
        case TransportError::invalid_argument:
            return "invalid argument";
        case TransportError::invalid_state:
            return "invalid state";
        case TransportError::end_of_stream:
            return "end of stream";
        case TransportError::would_block:
            return "would block";
        case TransportError::unsupported:
            return "unsupported";
        case TransportError::buffer_limit:
            return "buffer limit";
        case TransportError::callback_error:
            return "callback error";
        case TransportError::system_error:
            return "system error";
    }
    return "unknown transport error";
}

ControlSocketResult adopt_control_socket(int fd) noexcept {
    if (fd < 0) {
        return {{}, TransportError::invalid_argument, EINVAL};
    }
    if (!configure_descriptor(fd)) {
        const auto saved = errno;
        static_cast<void>(::close(fd));
        return {{}, TransportError::system_error, saved};
    }
    return {ControlSocket(fd), TransportError::none, 0};
}

ControlSocketResult connect_tcp(const std::string& host, std::uint16_t port) {
    if (host.empty()) {
        return {{}, TransportError::invalid_argument, EINVAL};
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    const auto lookup = ::getaddrinfo(host.c_str(), service.c_str(), &hints,
                                      &addresses);
    if (lookup != 0) {
        return {{}, TransportError::system_error, lookup};
    }
    int last_error = ECONNREFUSED;
    for (auto* current = addresses; current != nullptr;
         current = current->ai_next) {
        const auto fd =
            ::socket(current->ai_family, current->ai_socktype,
                     current->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        if (::connect(fd, current->ai_addr, current->ai_addrlen) == 0) {
            ::freeaddrinfo(addresses);
            return adopt_control_socket(fd);
        }
        last_error = errno;
        static_cast<void>(::close(fd));
    }
    ::freeaddrinfo(addresses);
    return {{}, TransportError::system_error, last_error};
}

ControlListenerResult listen_tcp(const std::string& host, std::uint16_t port,
                                 int backlog) {
    if (backlog <= 0) {
        return {{}, TransportError::invalid_argument, EINVAL};
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    const auto lookup = ::getaddrinfo(host.empty() ? nullptr : host.c_str(),
                                      service.c_str(), &hints, &addresses);
    if (lookup != 0) {
        return {{}, TransportError::system_error, lookup};
    }
    int last_error = EADDRNOTAVAIL;
    for (auto* current = addresses; current != nullptr;
         current = current->ai_next) {
        const auto fd =
            ::socket(current->ai_family, current->ai_socktype,
                     current->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        const int enabled = 1;
        static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled,
                                      sizeof(enabled)));
        if (::bind(fd, current->ai_addr, current->ai_addrlen) == 0 &&
            ::listen(fd, backlog) == 0 && configure_descriptor(fd)) {
            ::freeaddrinfo(addresses);
            return {ControlListener(fd, {}), TransportError::none, 0};
        }
        last_error = errno;
        static_cast<void>(::close(fd));
    }
    ::freeaddrinfo(addresses);
    return {{}, TransportError::system_error, last_error};
}

ControlSocketResult connect_unix(const std::string& path) {
    if (!valid_unix_path(path)) {
        return {{}, TransportError::invalid_argument,
                path.empty() ? EINVAL : ENAMETOOLONG};
    }
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return {{}, TransportError::system_error, errno};
    }
    sockaddr_un address{};
    const auto size = unix_address(path, address);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&address), size) != 0) {
        const auto saved = errno;
        static_cast<void>(::close(fd));
        return {{}, TransportError::system_error, saved};
    }
    return adopt_control_socket(fd);
}

ControlListenerResult listen_unix(const std::string& path, int backlog) {
    if (!valid_unix_path(path) || backlog <= 0) {
        return {{}, TransportError::invalid_argument,
                path.empty() || backlog <= 0 ? EINVAL : ENAMETOOLONG};
    }
    const auto fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return {{}, TransportError::system_error, errno};
    }
    sockaddr_un address{};
    const auto size = unix_address(path, address);
    const auto bound =
        ::bind(fd, reinterpret_cast<const sockaddr*>(&address), size) == 0;
    if (!bound || ::listen(fd, backlog) != 0 || !configure_descriptor(fd)) {
        const auto saved = errno;
        static_cast<void>(::close(fd));
        if (bound) {
            static_cast<void>(::unlink(path.c_str()));
        }
        return {{}, TransportError::system_error, saved};
    }
    return {ControlListener(fd, path), TransportError::none, 0};
}

}  // namespace shmipc::transport
