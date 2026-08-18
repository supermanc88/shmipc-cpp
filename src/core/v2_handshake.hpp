#pragma once

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

inline constexpr std::uint8_t v2_protocol_version = 2U;
inline constexpr std::uint32_t v2_max_metadata_frame_length =
    static_cast<std::uint32_t>(protocol::header_size + 4U + 2U * 65535U);

enum class V2HandshakeError {
    none,
    invalid_argument,
    transport_error,
    codec_error,
    unexpected_header,
    mapping_error,
    buffer_pool_error,
    queue_error,
};

struct V2HandshakeStatus {
    V2HandshakeError error{V2HandshakeError::none};
    int system_error{0};
    transport::TransportError transport_error{transport::TransportError::none};
    protocol::CodecError codec_error{protocol::CodecError::none};
    shm::MappingError mapping_error{shm::MappingError::none};
    shm::BufferPoolError buffer_pool_error{shm::BufferPoolError::none};
    shm::QueueError queue_error{shm::QueueError::none};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == V2HandshakeError::none;
    }
};

struct V2ClientConfig {
    std::string queue_path{};
    std::string buffer_path{};
    std::uint32_t queue_capacity{0};
    std::size_t buffer_size{0};
    std::vector<shm::BufferTierSpec> buffer_tiers{};
};

struct V2HandshakeResult;

class V2SharedMemory final {
public:
    V2SharedMemory() noexcept = default;
    ~V2SharedMemory() = default;

    V2SharedMemory(const V2SharedMemory&) = delete;
    V2SharedMemory& operator=(const V2SharedMemory&) = delete;
    V2SharedMemory(V2SharedMemory&&) noexcept = default;
    V2SharedMemory& operator=(V2SharedMemory&&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] bool is_creator() const noexcept;
    [[nodiscard]] const std::string& queue_path() const noexcept;
    [[nodiscard]] const std::string& buffer_path() const noexcept;
    [[nodiscard]] shm::BufferPool& buffer_pool() noexcept;
    [[nodiscard]] shm::SharedQueue& send_queue() noexcept;
    [[nodiscard]] shm::SharedQueue& receive_queue() noexcept;

private:
    friend struct V2HandshakeResult;
    friend V2HandshakeResult v2_client_handshake(
        transport::ControlSocket&, const V2ClientConfig&);
    friend V2HandshakeResult v2_server_handshake(
        transport::ControlSocket&, std::uint32_t);

    V2SharedMemory(shm::SharedMemoryRegion&& buffer_region,
                   shm::SharedMemoryRegion&& queue_region,
                   shm::BufferPool&& buffer_pool,
                   shm::SharedQueue&& send_queue,
                   shm::SharedQueue&& receive_queue,
                   bool creator) noexcept;

    shm::SharedMemoryRegion buffer_region_{};
    shm::SharedMemoryRegion queue_region_{};
    shm::BufferPool buffer_pool_{};
    shm::SharedQueue send_queue_{};
    shm::SharedQueue receive_queue_{};
    bool creator_{false};
};

struct V2HandshakeResult {
    V2SharedMemory value{};
    V2HandshakeStatus status{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(status);
    }
};

[[nodiscard]] const char* to_string(V2HandshakeError error) noexcept;

[[nodiscard]] V2HandshakeResult v2_client_handshake(
    transport::ControlSocket& socket, const V2ClientConfig& config);
[[nodiscard]] V2HandshakeResult v2_server_handshake(
    transport::ControlSocket& socket,
    std::uint32_t max_frame_length = v2_max_metadata_frame_length);

}  // namespace shmipc::core
