#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace shmipc {

enum class Error {
    none,
    invalid_argument,
    unsupported,
    connection_error,
    handshake_error,
    event_loop_error,
    transport_error,
    protocol_error,
    shared_memory_error,
    unhealthy,
    closed,
    timeout,
};

struct Status {
    Error error{Error::none};
    int system_error{0};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == Error::none;
    }
};

[[nodiscard]] const char* to_string(Error error) noexcept;

enum class SharedMemoryMode {
    file,
    memfd,
};

struct BufferTier {
    std::uint32_t capacity{0};
    std::uint32_t percent{0};
};

struct ClientConfig {
    SharedMemoryMode shared_memory_mode{SharedMemoryMode::file};

    // File mode treats these values as filesystem paths. Memfd mode treats
    // them as diagnostic names and requires a Unix domain control socket.
    std::string queue_name{"/dev/shm/shmipc_queue"};
    std::string buffer_name{"/dev/shm/shmipc"};
    std::uint32_t queue_capacity{8192U};
    std::size_t buffer_size{32U * 1024U * 1024U};
    std::vector<BufferTier> buffer_tiers{
        {8192U - 20U, 50U},
        {32U * 1024U - 20U, 30U},
        {128U * 1024U - 20U, 20U},
    };
};

struct MessageResult;
struct StreamResult;
struct SessionResult;

// Move-only stream handle. Operations on distinct streams may run
// concurrently. Calls that mutate the same stream are serialized internally.
class Stream final {
public:
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    Stream() noexcept;
    ~Stream();

    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) noexcept;
    Stream& operator=(Stream&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint32_t id() const noexcept;
    [[nodiscard]] bool is_fallback() const noexcept;

    [[nodiscard]] Status send(const std::uint8_t* data, std::size_t size);
    [[nodiscard]] Status send(const std::vector<std::uint8_t>& data);
    [[nodiscard]] MessageResult receive(std::chrono::milliseconds timeout);

    void set_deadline(std::optional<Deadline> deadline) noexcept;
    void set_read_deadline(std::optional<Deadline> deadline) noexcept;
    void set_write_deadline(std::optional<Deadline> deadline) noexcept;

    [[nodiscard]] Status close();
    [[nodiscard]] Status wait_remote_close(std::chrono::milliseconds timeout);

private:
    friend class Session;
    struct Impl;

    explicit Stream(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct MessageResult {
    std::vector<std::uint8_t> value{};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

struct StreamResult {
    Stream value{};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

// Move-only client session. It owns its event-loop thread and closes all
// streams before joining that thread during destruction.
class Session final {
public:
    Session() noexcept;
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool is_healthy() const noexcept;
    [[nodiscard]] StreamResult open_stream();
    [[nodiscard]] Status close() noexcept;

private:
    friend SessionResult connect_tcp(const std::string&, std::uint16_t,
                                     const ClientConfig&);
    friend SessionResult connect_unix(const std::string&, const ClientConfig&);
    struct Impl;

    explicit Session(std::unique_ptr<Impl> impl) noexcept;
    [[nodiscard]] static SessionResult start(int socket_descriptor,
                                             const ClientConfig& config);
    std::unique_ptr<Impl> impl_;
};

struct SessionResult {
    Session value{};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

// File-backed shared memory works with TCP or Unix control sockets. Memfd
// descriptor transfer is supported only by connect_unix().
[[nodiscard]] SessionResult connect_tcp(const std::string& host,
                                        std::uint16_t port,
                                        const ClientConfig& config = {});
[[nodiscard]] SessionResult connect_unix(const std::string& path,
                                         const ClientConfig& config = {});

}  // namespace shmipc
