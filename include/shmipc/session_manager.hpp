#pragma once

#include "shmipc/session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace shmipc {

struct SessionManagerConfig {
    ClientConfig client_config{};
    std::size_t session_count{1U};
    std::size_t max_idle_streams_per_session{4096U};
    std::size_t round_robin_batch{32U};
    std::chrono::milliseconds reconnect_interval{500};
    std::chrono::milliseconds health_check_interval{50};
};

struct PooledStreamResult;
struct SessionManagerResult;

// RAII lease for a SessionManager Stream. Destruction returns a reusable
// Stream to its originating pool, or closes it when reuse is unsafe.
class PooledStream final {
public:
    PooledStream() noexcept;
    ~PooledStream();

    PooledStream(const PooledStream&) = delete;
    PooledStream& operator=(const PooledStream&) = delete;
    PooledStream(PooledStream&&) noexcept;
    PooledStream& operator=(PooledStream&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    // Precondition: this lease is valid.
    [[nodiscard]] Stream& stream() noexcept;
    [[nodiscard]] const Stream& stream() const noexcept;
    [[nodiscard]] std::size_t session_index() const noexcept;
    [[nodiscard]] Status return_to_pool() noexcept;
    [[nodiscard]] Status close() noexcept;

private:
    friend class SessionManager;
    struct Impl;

    explicit PooledStream(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct PooledStreamResult {
    PooledStream value{};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

// Multi-Session client with batched round-robin selection, idle Stream reuse,
// and per-Session background reconnection after control-connection failure.
// get_stream() may run concurrently with other get_stream() calls and close().
class SessionManager final {
public:
    SessionManager() noexcept;
    ~SessionManager();

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) noexcept;
    SessionManager& operator=(SessionManager&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::size_t session_count() const noexcept;
    [[nodiscard]] PooledStreamResult get_stream();
    [[nodiscard]] Status close() noexcept;

private:
    friend SessionManagerResult make_tcp_session_manager(
        const std::string&, std::uint16_t, const SessionManagerConfig&);
    friend SessionManagerResult make_unix_session_manager(
        const std::string&, const SessionManagerConfig&);
    struct Impl;

    explicit SessionManager(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> impl_;
};

struct SessionManagerResult {
    SessionManager value{};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

[[nodiscard]] SessionManagerResult make_tcp_session_manager(
    const std::string& host, std::uint16_t port,
    const SessionManagerConfig& config = {});
[[nodiscard]] SessionManagerResult make_unix_session_manager(
    const std::string& path, const SessionManagerConfig& config = {});

}  // namespace shmipc
