#pragma once

#include "core/protocol_version_negotiation.hpp"
#include "protocol/control_codec.hpp"
#include "shm/buffer_pool.hpp"
#include "shm/shared_memory_region.hpp"
#include "shm/shared_queue.hpp"
#include "transport/control_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace shmipc::core {

inline constexpr std::uint8_t v3_protocol_version = 3U;
inline constexpr std::uint32_t v3_max_metadata_frame_length =
    static_cast<std::uint32_t>(protocol::header_size + 4U + 2U * 65535U);

enum class V3HandshakeError {
    none,
    invalid_argument,
    unsupported,
    version_negotiation_error,
    unsupported_version,
    transport_error,
    codec_error,
    unexpected_header,
    descriptor_count_error,
    mapping_error,
    buffer_pool_error,
    queue_error,
};

struct V3HandshakeStatus {
    V3HandshakeError error{V3HandshakeError::none};
    int system_error{0};
    ProtocolVersionNegotiationStatus negotiation_status{};
    transport::TransportError transport_error{transport::TransportError::none};
    protocol::CodecError codec_error{protocol::CodecError::none};
    shm::MappingError mapping_error{shm::MappingError::none};
    shm::BufferPoolError buffer_pool_error{shm::BufferPoolError::none};
    shm::QueueError queue_error{shm::QueueError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == V3HandshakeError::none;
    }
};

struct V3ClientConfig {
    std::string queue_name{};
    std::string buffer_name{};
    std::uint32_t queue_capacity{0};
    std::size_t buffer_size{0};
    std::vector<shm::BufferTierSpec> buffer_tiers{};
};

struct V3HandshakeResult;

class V3SharedMemory final {
  public:
    V3SharedMemory() noexcept = default;
    ~V3SharedMemory() = default;

    V3SharedMemory(const V3SharedMemory&) = delete;
    V3SharedMemory& operator=(const V3SharedMemory&) = delete;
    V3SharedMemory(V3SharedMemory&&) noexcept = default;
    V3SharedMemory& operator=(V3SharedMemory&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool is_creator() const noexcept;
    [[nodiscard]] const std::string& queue_name() const noexcept;
    [[nodiscard]] const std::string& buffer_name() const noexcept;
    [[nodiscard]] int queue_fd() const noexcept;
    [[nodiscard]] int buffer_fd() const noexcept;
    [[nodiscard]] shm::BufferPool& buffer_pool() noexcept;
    [[nodiscard]] shm::SharedQueue& send_queue() noexcept;
    [[nodiscard]] shm::SharedQueue& receive_queue() noexcept;

  private:
    friend struct V3HandshakeResult;
    friend V3HandshakeResult v3_client_handshake(transport::ControlSocket&,
                                                 const V3ClientConfig&);
    friend V3HandshakeResult v3_server_handshake(transport::ControlSocket&,
                                                 std::uint32_t);

    V3SharedMemory(shm::SharedMemoryRegion&& buffer_region,
                   shm::SharedMemoryRegion&& queue_region,
                   shm::BufferPool&& buffer_pool, shm::SharedQueue&& send_queue,
                   shm::SharedQueue&& receive_queue, std::string queue_name,
                   std::string buffer_name, bool creator) noexcept;

    shm::SharedMemoryRegion buffer_region_{};
    shm::SharedMemoryRegion queue_region_{};
    shm::BufferPool buffer_pool_{};
    shm::SharedQueue send_queue_{};
    shm::SharedQueue receive_queue_{};
    std::string queue_name_{};
    std::string buffer_name_{};
    bool creator_{false};
};

struct V3HandshakeResult {
    V3SharedMemory value{};
    V3HandshakeStatus status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

[[nodiscard]] const char* to_string(V3HandshakeError error) noexcept;
[[nodiscard]] V3HandshakeResult
v3_client_handshake(transport::ControlSocket& socket,
                    const V3ClientConfig& config);
[[nodiscard]] V3HandshakeResult v3_server_handshake(
    transport::ControlSocket& socket,
    std::uint32_t max_frame_length = v3_max_metadata_frame_length);

} // namespace shmipc::core
