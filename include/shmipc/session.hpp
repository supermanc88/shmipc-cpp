#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    callback_already_set,
    callback_error,
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
struct CallbackSubscriptionResult;
class CallbackExecutor;
class StreamCallbacks;
struct AsyncCallbackState;

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
    [[nodiscard]] CallbackSubscriptionResult set_callbacks(
        std::shared_ptr<StreamCallbacks> callbacks,
        std::shared_ptr<CallbackExecutor> executor);

private:
    friend class Session;
    friend struct AsyncCallbackState;
    struct Impl;

    explicit Stream(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> impl_;
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

class StreamCallbacks {
public:
    virtual ~StreamCallbacks() = default;

    virtual void on_data(Stream& stream, std::vector<std::uint8_t> data) = 0;
    virtual void on_local_close(Stream& stream);
    virtual void on_remote_close(Stream& stream);
    virtual void on_error(Stream& stream, const Status& status);
};

// Shared callback thread pool. Each Stream is serialized, while different
// Streams may execute concurrently up to thread_count().
class CallbackExecutor final {
public:
    explicit CallbackExecutor(std::size_t thread_count = 1U);
    ~CallbackExecutor();

    CallbackExecutor(const CallbackExecutor&) = delete;
    CallbackExecutor& operator=(const CallbackExecutor&) = delete;
    CallbackExecutor(CallbackExecutor&&) = delete;
    CallbackExecutor& operator=(CallbackExecutor&&) = delete;

    [[nodiscard]] std::size_t thread_count() const noexcept;

private:
    friend struct AsyncCallbackState;
    struct Impl;

    [[nodiscard]] bool execute(std::function<void()> task);
    std::unique_ptr<Impl> impl_;
};

// RAII registration. stop() removes the callbacks without closing the Stream
// and waits for an in-flight callback unless called from an executor callback.
class CallbackSubscription final {
public:
    CallbackSubscription() noexcept;
    ~CallbackSubscription();

    CallbackSubscription(const CallbackSubscription&) = delete;
    CallbackSubscription& operator=(const CallbackSubscription&) = delete;
    CallbackSubscription(CallbackSubscription&&) noexcept;
    CallbackSubscription& operator=(CallbackSubscription&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] Status stop() noexcept;

private:
    friend struct CallbackSubscriptionResult;
    friend class Stream;
    struct Impl;

    explicit CallbackSubscription(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct CallbackSubscriptionResult {
    CallbackSubscription value{};
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
