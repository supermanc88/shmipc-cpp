#pragma once

#include "core/v2_client_session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace shmipc::core {

struct V2MultiplexedSessionState;
struct V2StreamState;

class V2Stream final {
public:
  V2Stream() noexcept = default;
  ~V2Stream();

  V2Stream(const V2Stream &) = delete;
  V2Stream &operator=(const V2Stream &) = delete;
  V2Stream(V2Stream &&) noexcept = default;
  V2Stream &operator=(V2Stream &&other) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] std::uint32_t id() const noexcept;
  [[nodiscard]] V2SessionStatus send(const std::uint8_t *data,
                                     std::size_t size);
  [[nodiscard]] V2SessionStatus send(const std::vector<std::uint8_t> &data);
  struct MessageResult {
    std::vector<std::uint8_t> value{};
    V2SessionStatus status{};

    [[nodiscard]] explicit operator bool() const noexcept {
      return static_cast<bool>(status);
    }
  };

  [[nodiscard]] MessageResult receive(std::chrono::milliseconds timeout);
  [[nodiscard]] V2SessionStatus close();
  [[nodiscard]] V2SessionStatus
  wait_remote_close(std::chrono::milliseconds timeout);

private:
  friend class V2MultiplexedClientSession;
  friend class V2MultiplexedServerSession;

  V2Stream(std::shared_ptr<V2MultiplexedSessionState> session,
           std::shared_ptr<V2StreamState> stream,
           std::shared_ptr<transport::EventConnection> connection) noexcept;

  std::shared_ptr<V2MultiplexedSessionState> session_{};
  std::shared_ptr<V2StreamState> stream_{};
  std::shared_ptr<transport::EventConnection> connection_{};
};

struct V2StreamResult {
  V2Stream value{};
  V2SessionStatus status{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(status);
  }
};

struct V2MultiplexedClientSessionResult;

class V2MultiplexedClientSession final {
public:
  V2MultiplexedClientSession() noexcept = default;
  ~V2MultiplexedClientSession();

  V2MultiplexedClientSession(const V2MultiplexedClientSession &) = delete;
  V2MultiplexedClientSession &
  operator=(const V2MultiplexedClientSession &) = delete;
  V2MultiplexedClientSession(V2MultiplexedClientSession &&) noexcept = default;
  V2MultiplexedClientSession &
  operator=(V2MultiplexedClientSession &&) noexcept = delete;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] V2StreamResult open_stream();
  [[nodiscard]] V2SessionStatus close() noexcept;

private:
  friend struct V2MultiplexedClientSessionResult;
  friend V2MultiplexedClientSessionResult
  start_v2_multiplexed_client_session(transport::ControlSocket &&,
                                      const V2ClientConfig &,
                                      transport::EpollDispatcher &);

  V2MultiplexedClientSession(
      std::shared_ptr<V2MultiplexedSessionState> state,
      std::shared_ptr<transport::EventConnection> connection) noexcept;

  std::shared_ptr<V2MultiplexedSessionState> state_{};
  std::shared_ptr<transport::EventConnection> connection_{};
};

struct V2MultiplexedClientSessionResult {
  V2MultiplexedClientSession value{};
  V2SessionStatus status{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(status);
  }
};

struct V2MultiplexedServerSessionResult;

class V2MultiplexedServerSession final {
public:
  V2MultiplexedServerSession() noexcept = default;
  ~V2MultiplexedServerSession();

  V2MultiplexedServerSession(const V2MultiplexedServerSession &) = delete;
  V2MultiplexedServerSession &
  operator=(const V2MultiplexedServerSession &) = delete;
  V2MultiplexedServerSession(V2MultiplexedServerSession &&) noexcept = default;
  V2MultiplexedServerSession &
  operator=(V2MultiplexedServerSession &&) noexcept = delete;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] bool is_open() const noexcept;
  [[nodiscard]] V2StreamResult accept_stream(std::chrono::milliseconds timeout);
  [[nodiscard]] V2SessionStatus close() noexcept;

private:
  friend struct V2MultiplexedServerSessionResult;
  friend V2MultiplexedServerSessionResult start_v2_multiplexed_server_session(
      transport::ControlSocket &&, transport::EpollDispatcher &, std::uint32_t);

  V2MultiplexedServerSession(
      std::shared_ptr<V2MultiplexedSessionState> state,
      std::shared_ptr<transport::EventConnection> connection) noexcept;

  std::shared_ptr<V2MultiplexedSessionState> state_{};
  std::shared_ptr<transport::EventConnection> connection_{};
};

struct V2MultiplexedServerSessionResult {
  V2MultiplexedServerSession value{};
  V2SessionStatus status{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(status);
  }
};

[[nodiscard]] V2MultiplexedClientSessionResult
start_v2_multiplexed_client_session(transport::ControlSocket &&socket,
                                    const V2ClientConfig &config,
                                    transport::EpollDispatcher &dispatcher);

[[nodiscard]] V2MultiplexedServerSessionResult
start_v2_multiplexed_server_session(
    transport::ControlSocket &&socket, transport::EpollDispatcher &dispatcher,
    std::uint32_t max_frame_length = v2_max_metadata_frame_length);

} // namespace shmipc::core
