#include "shmipc/stream_connection.hpp"

#include "public/session_impl.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace shmipc {

struct StreamConnection::Impl final {
    explicit Impl(Stream&& value) : stream(std::move(value)) {}

    Stream stream{};
    std::mutex read_mutex{};
    std::vector<std::uint8_t> pending{};
    std::size_t pending_offset{0U};
    std::optional<Status> deferred_status{};
};

StreamConnection::StreamConnection() noexcept = default;
StreamConnection::StreamConnection(Stream&& stream) {
    if (stream) {
        impl_ = std::make_unique<Impl>(std::move(stream));
    }
}
StreamConnection::~StreamConnection() {
    static_cast<void>(close());
}
StreamConnection::StreamConnection(StreamConnection&&) noexcept = default;
StreamConnection& StreamConnection::operator=(StreamConnection&&) noexcept =
    default;

StreamConnection::operator bool() const noexcept {
    return impl_ != nullptr && static_cast<bool>(impl_->stream);
}

std::uint32_t StreamConnection::id() const noexcept {
    return impl_ == nullptr ? 0U : impl_->stream.id();
}

TransferResult StreamConnection::read(std::uint8_t* data, std::size_t size,
                                      std::chrono::milliseconds timeout) {
    if (impl_ == nullptr) {
        return {0U, detail::make_status(Error::closed, EBADF)};
    }
    if ((data == nullptr && size != 0U) || timeout.count() < 0) {
        return {0U, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    if (size == 0U) {
        return {};
    }
    std::lock_guard<std::mutex> lock(impl_->read_mutex);
    std::size_t transferred = 0U;
    for (;;) {
        const auto available = impl_->pending.size() - impl_->pending_offset;
        if (available != 0U) {
            const auto amount = std::min(size - transferred, available);
            std::memcpy(data + transferred,
                        impl_->pending.data() + impl_->pending_offset, amount);
            transferred += amount;
            impl_->pending_offset += amount;
            if (impl_->pending_offset == impl_->pending.size()) {
                impl_->pending.clear();
                impl_->pending_offset = 0U;
            }
            if (transferred == size) {
                return {transferred, {}};
            }
        }
        if (impl_->deferred_status) {
            if (transferred != 0U) {
                return {transferred, {}};
            }
            const auto status = *impl_->deferred_status;
            impl_->deferred_status.reset();
            return {0U, status};
        }
        auto received = impl_->stream.receive(
            transferred == 0U ? timeout : std::chrono::milliseconds(0));
        if (!received) {
            if (transferred == 0U) {
                return {0U, received.status};
            }
            if (received.status.error != Error::timeout) {
                impl_->deferred_status = received.status;
            }
            return {transferred, {}};
        }
        impl_->pending = std::move(received.value);
        impl_->pending_offset = 0U;
    }
}

TransferResult StreamConnection::write(const std::uint8_t* data,
                                       std::size_t size) {
    if (impl_ == nullptr) {
        return {0U, detail::make_status(Error::closed, EBADF)};
    }
    if (data == nullptr && size != 0U) {
        return {0U, detail::make_status(Error::invalid_argument, EINVAL)};
    }
    if (size == 0U) {
        return {};
    }
    const auto status = impl_->stream.send(data, size);
    return {status ? size : 0U, status};
}

void StreamConnection::set_deadline(
    std::optional<Stream::Deadline> deadline) noexcept {
    if (impl_ != nullptr) {
        impl_->stream.set_deadline(deadline);
    }
}

void StreamConnection::set_read_deadline(
    std::optional<Stream::Deadline> deadline) noexcept {
    if (impl_ != nullptr) {
        impl_->stream.set_read_deadline(deadline);
    }
}

void StreamConnection::set_write_deadline(
    std::optional<Stream::Deadline> deadline) noexcept {
    if (impl_ != nullptr) {
        impl_->stream.set_write_deadline(deadline);
    }
}

Status StreamConnection::close() {
    if (impl_ == nullptr) {
        return {};
    }
    const auto status = impl_->stream.close();
    impl_.reset();
    return status;
}

}  // namespace shmipc
