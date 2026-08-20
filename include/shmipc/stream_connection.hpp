#pragma once

#include "shmipc/session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace shmipc {

struct TransferResult {
    std::size_t transferred{0U};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

// Copy-based byte-stream compatibility adapter. write() publishes one shmipc
// message; read() hides message boundaries and retains any unread suffix.
// One reader and one writer may run concurrently; close must be externally
// synchronized with other operations on the same object.
class StreamConnection final {
public:
    StreamConnection() noexcept;
    explicit StreamConnection(Stream&& stream);
    ~StreamConnection();

    StreamConnection(const StreamConnection&) = delete;
    StreamConnection& operator=(const StreamConnection&) = delete;
    StreamConnection(StreamConnection&&) noexcept;
    StreamConnection& operator=(StreamConnection&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint32_t id() const noexcept;
    [[nodiscard]] TransferResult read(std::uint8_t* data, std::size_t size,
                                      std::chrono::milliseconds timeout);
    [[nodiscard]] TransferResult write(const std::uint8_t* data,
                                       std::size_t size);

    void set_deadline(std::optional<Stream::Deadline> deadline) noexcept;
    void set_read_deadline(std::optional<Stream::Deadline> deadline) noexcept;
    void set_write_deadline(std::optional<Stream::Deadline> deadline) noexcept;
    [[nodiscard]] Status close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace shmipc
