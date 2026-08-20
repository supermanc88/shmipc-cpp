#pragma once

#include "shmipc/session.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace shmipc {

struct ListenerConfig {
    SharedMemoryMode shared_memory_mode{SharedMemoryMode::file};
    int backlog{128};
    std::uint32_t max_handshake_frame_length{64U * 1024U * 1024U + 8U};
    std::shared_ptr<Monitor> monitor{};
    std::chrono::milliseconds metrics_interval{30000};
    std::shared_ptr<Logger> logger{};
    LogLevel log_level{LogLevel::warning};
};

struct ListenerResult;

// Move-only control listener. Accepted Sessions share the Listener event loop
// and remain usable after the Listener is closed. close() may run concurrently
// with accept_session(); other operations require external synchronization.
class Listener final {
public:
    Listener() noexcept;
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) noexcept;
    Listener& operator=(Listener&&) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] std::uint16_t local_port() const noexcept;
    // The timeout covers waiting for a control connection. Once accepted,
    // protocol negotiation runs to completion using the handshake framing
    // limits in ListenerConfig.
    [[nodiscard]] SessionResult accept_session(
        std::chrono::milliseconds timeout);
    [[nodiscard]] Status close() noexcept;

private:
    friend ListenerResult listen_tcp(const std::string&, std::uint16_t,
                                     const ListenerConfig&);
    friend ListenerResult listen_unix(const std::string&,
                                      const ListenerConfig&);
    struct Impl;

    explicit Listener(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> impl_;
};

struct ListenerResult {
    Listener value{};
    Status status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

[[nodiscard]] ListenerResult listen_tcp(
    const std::string& host, std::uint16_t port,
    const ListenerConfig& config = {});
[[nodiscard]] ListenerResult listen_unix(
    const std::string& path, const ListenerConfig& config = {});

}  // namespace shmipc
