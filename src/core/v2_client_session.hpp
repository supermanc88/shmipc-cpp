#pragma once

#include "core/v2_handshake.hpp"
#include "shm/buffer_io.hpp"
#include "transport/epoll_dispatcher.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace shmipc::core {

inline constexpr std::uint32_t v2_single_stream_id = 1U;

enum class V2SessionError {
    none,
    invalid_argument,
    handshake_error,
    dispatcher_error,
    transport_error,
    codec_error,
    unexpected_event,
    unexpected_stream,
    queue_error,
    buffer_pool_error,
    buffer_io_error,
    closed,
    timeout,
};

struct V2SessionStatus {
    V2SessionError error{V2SessionError::none};
    V2HandshakeStatus handshake_status{};
    int system_error{0};
    transport::TransportError transport_error{transport::TransportError::none};
    protocol::CodecError codec_error{protocol::CodecError::none};
    shm::QueueError queue_error{shm::QueueError::none};
    shm::BufferPoolError buffer_pool_error{shm::BufferPoolError::none};
    shm::BufferIoError buffer_io_error{shm::BufferIoError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == V2SessionError::none;
    }
};

struct V2ClientSessionState;
struct V2ClientSessionResult;

class V2ClientSession final {
public:
    V2ClientSession() noexcept = default;
    ~V2ClientSession();

    V2ClientSession(const V2ClientSession&) = delete;
    V2ClientSession& operator=(const V2ClientSession&) = delete;
    V2ClientSession(V2ClientSession&&) noexcept = default;
    V2ClientSession& operator=(V2ClientSession&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] V2SessionStatus send(const std::uint8_t* data,
                                       std::size_t size);
    [[nodiscard]] V2SessionStatus send(const std::vector<std::uint8_t>& data);
    [[nodiscard]] V2SessionStatus close_stream();
    [[nodiscard]] V2SessionStatus close() noexcept;
    [[nodiscard]] V2SessionStatus wait_remote_close(
        std::chrono::milliseconds timeout);

    struct MessageResult {
        std::vector<std::uint8_t> value{};
        V2SessionStatus status{};

        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(status);
        }
    };

    [[nodiscard]] MessageResult receive(std::chrono::milliseconds timeout);

private:
    friend struct V2ClientSessionResult;
    friend V2ClientSessionResult start_v2_client_session(
        transport::ControlSocket&&, const V2ClientConfig&,
        transport::EpollDispatcher&);

    V2ClientSession(std::shared_ptr<V2ClientSessionState> state,
                    std::shared_ptr<transport::EventConnection> connection)
        noexcept;

    std::shared_ptr<V2ClientSessionState> state_{};
    std::shared_ptr<transport::EventConnection> connection_{};
};

struct V2ClientSessionResult {
    V2ClientSession value{};
    V2SessionStatus status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

[[nodiscard]] const char* to_string(V2SessionError error) noexcept;

[[nodiscard]] V2ClientSessionResult start_v2_client_session(
    transport::ControlSocket&& socket, const V2ClientConfig& config,
    transport::EpollDispatcher& dispatcher);

}  // namespace shmipc::core
