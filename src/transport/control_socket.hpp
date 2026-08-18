#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace shmipc::transport {

enum class TransportError {
    none,
    invalid_argument,
    invalid_state,
    end_of_stream,
    would_block,
    unsupported,
    buffer_limit,
    callback_error,
    system_error,
};

template <typename T>
struct TransportResult {
    T value{};
    TransportError error{TransportError::none};
    int system_error{0};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == TransportError::none;
    }
};

struct IoProgress {
    std::size_t transferred{0};
};

using IoResult = TransportResult<IoProgress>;

class ControlSocket final {
public:
    ControlSocket() noexcept = default;
    ~ControlSocket();

    ControlSocket(const ControlSocket&) = delete;
    ControlSocket& operator=(const ControlSocket&) = delete;
    ControlSocket(ControlSocket&& other) noexcept;
    ControlSocket& operator=(ControlSocket&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] int native_handle() const noexcept;
    [[nodiscard]] int release() noexcept;
    [[nodiscard]] TransportError close() noexcept;
    [[nodiscard]] TransportError shutdown() noexcept;
    [[nodiscard]] TransportError set_nonblocking(bool enabled) noexcept;
    [[nodiscard]] IoResult read_full(std::uint8_t* data,
                                     std::size_t size) noexcept;
    [[nodiscard]] IoResult write_full(const std::uint8_t* data,
                                      std::size_t size) noexcept;

private:
    friend TransportResult<ControlSocket> adopt_control_socket(int) noexcept;
    explicit ControlSocket(int fd) noexcept;

    int fd_{-1};
};

using ControlSocketResult = TransportResult<ControlSocket>;

class ControlListener final {
public:
    ControlListener() noexcept = default;
    ~ControlListener();

    ControlListener(const ControlListener&) = delete;
    ControlListener& operator=(const ControlListener&) = delete;
    ControlListener(ControlListener&& other) noexcept;
    ControlListener& operator=(ControlListener&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] int native_handle() const noexcept;
    [[nodiscard]] ControlSocketResult accept() noexcept;
    [[nodiscard]] TransportResult<std::uint16_t> local_port() const noexcept;
    [[nodiscard]] TransportError close() noexcept;

private:
    friend TransportResult<ControlListener> listen_tcp(
        const std::string&, std::uint16_t, int);
    friend TransportResult<ControlListener> listen_unix(
        const std::string&, int);

    ControlListener(int fd, std::string unix_path) noexcept;

    int fd_{-1};
    std::string unix_path_{};
};

using ControlListenerResult = TransportResult<ControlListener>;

[[nodiscard]] const char* to_string(TransportError error) noexcept;

// Takes ownership of fd even when descriptor configuration fails.
[[nodiscard]] ControlSocketResult adopt_control_socket(int fd) noexcept;
[[nodiscard]] ControlSocketResult connect_tcp(const std::string& host,
                                              std::uint16_t port);
[[nodiscard]] ControlListenerResult listen_tcp(const std::string& host,
                                               std::uint16_t port,
                                               int backlog = 128);
[[nodiscard]] ControlSocketResult connect_unix(const std::string& path);
[[nodiscard]] ControlListenerResult listen_unix(const std::string& path,
                                                int backlog = 128);

}  // namespace shmipc::transport
