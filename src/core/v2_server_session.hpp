#pragma once

#include "core/v2_client_session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace shmipc::core {

struct V2ServerSessionResult;

class V2ServerSession final {
public:
    V2ServerSession() noexcept = default;
    ~V2ServerSession();

    V2ServerSession(const V2ServerSession&) = delete;
    V2ServerSession& operator=(const V2ServerSession&) = delete;
    V2ServerSession(V2ServerSession&&) noexcept = default;
    V2ServerSession& operator=(V2ServerSession&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::uint32_t stream_id() const noexcept;
    [[nodiscard]] V2SessionStatus
    wait_stream(std::chrono::milliseconds timeout);
    [[nodiscard]] V2SessionStatus send(const std::uint8_t* data,
                                       std::size_t size);
    [[nodiscard]] V2SessionStatus send(const std::vector<std::uint8_t>& data);
    [[nodiscard]] V2SessionStatus close_stream();
    [[nodiscard]] V2SessionStatus close() noexcept;
    [[nodiscard]] V2SessionStatus
    wait_remote_close(std::chrono::milliseconds timeout);

    using MessageResult = V2ClientSession::MessageResult;
    [[nodiscard]] MessageResult receive(std::chrono::milliseconds timeout);

private:
    friend struct V2ServerSessionResult;
    friend V2ServerSessionResult
    start_v2_server_session(transport::ControlSocket&&,
                            transport::EpollDispatcher&, std::uint32_t);

    V2ServerSession(
        std::shared_ptr<V2SingleStreamSessionState> state,
        std::shared_ptr<transport::EventConnection> connection) noexcept;

    std::shared_ptr<V2SingleStreamSessionState> state_{};
    std::shared_ptr<transport::EventConnection> connection_{};
};

struct V2ServerSessionResult {
    V2ServerSession value{};
    V2SessionStatus status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

[[nodiscard]] V2ServerSessionResult start_v2_server_session(
    transport::ControlSocket&& socket, transport::EpollDispatcher& dispatcher,
    std::uint32_t max_frame_length = v2_max_metadata_frame_length);

}  // namespace shmipc::core
