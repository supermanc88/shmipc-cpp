#include "core/v2_handshake.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

namespace shmipc::core {

namespace {

V2HandshakeResult failed(V2HandshakeStatus status) {
    return {{}, status};
}

V2HandshakeStatus transport_failure(const transport::IoResult& result) {
    V2HandshakeStatus status{};
    status.error = V2HandshakeError::transport_error;
    status.system_error = result.system_error;
    status.transport_error = result.error;
    return status;
}

V2HandshakeStatus mapping_failure(const shm::MappingStatus& result) {
    V2HandshakeStatus status{};
    status.error = V2HandshakeError::mapping_error;
    status.system_error = result.system_error;
    status.mapping_error = result.error;
    return status;
}

V2HandshakeStatus codec_failure(protocol::CodecError error) {
    V2HandshakeStatus status{};
    status.error = V2HandshakeError::codec_error;
    status.codec_error = error;
    return status;
}

V2HandshakeStatus pool_failure(shm::BufferPoolError error) {
    V2HandshakeStatus status{};
    status.error = V2HandshakeError::buffer_pool_error;
    status.buffer_pool_error = error;
    return status;
}

V2HandshakeStatus queue_failure(shm::QueueError error) {
    V2HandshakeStatus status{};
    status.error = V2HandshakeError::queue_error;
    status.queue_error = error;
    return status;
}

bool valid_client_config(const V2ClientConfig& config) {
    return !config.queue_path.empty() && !config.buffer_path.empty() &&
           config.queue_path != config.buffer_path &&
           config.queue_capacity != 0U && config.buffer_size != 0U &&
           config.buffer_size <= std::numeric_limits<std::uint32_t>::max() &&
           !config.buffer_tiers.empty();
}

}  // namespace

V2SharedMemory::V2SharedMemory(shm::SharedMemoryRegion&& buffer_region,
                               shm::SharedMemoryRegion&& queue_region,
                               shm::BufferPool&& buffer_pool,
                               shm::SharedQueue&& send_queue,
                               shm::SharedQueue&& receive_queue,
                               bool creator) noexcept
    : buffer_region_(std::move(buffer_region)),
      queue_region_(std::move(queue_region)),
      buffer_pool_(std::move(buffer_pool)),
      send_queue_(std::move(send_queue)),
      receive_queue_(std::move(receive_queue)),
      creator_(creator) {}

V2SharedMemory::operator bool() const noexcept {
    return static_cast<bool>(buffer_region_) &&
           static_cast<bool>(queue_region_) &&
           static_cast<bool>(buffer_pool_) &&
           static_cast<bool>(send_queue_) &&
           static_cast<bool>(receive_queue_);
}

bool V2SharedMemory::is_creator() const noexcept { return creator_; }

const std::string& V2SharedMemory::queue_path() const noexcept {
    return queue_region_.path();
}

const std::string& V2SharedMemory::buffer_path() const noexcept {
    return buffer_region_.path();
}

shm::BufferPool& V2SharedMemory::buffer_pool() noexcept {
    return buffer_pool_;
}

shm::SharedQueue& V2SharedMemory::send_queue() noexcept {
    return send_queue_;
}

shm::SharedQueue& V2SharedMemory::receive_queue() noexcept {
    return receive_queue_;
}

const char* to_string(V2HandshakeError error) noexcept {
    switch (error) {
        case V2HandshakeError::none:
            return "none";
        case V2HandshakeError::invalid_argument:
            return "invalid argument";
        case V2HandshakeError::transport_error:
            return "transport error";
        case V2HandshakeError::codec_error:
            return "codec error";
        case V2HandshakeError::unexpected_header:
            return "unexpected header";
        case V2HandshakeError::mapping_error:
            return "mapping error";
        case V2HandshakeError::buffer_pool_error:
            return "buffer pool error";
        case V2HandshakeError::queue_error:
            return "queue error";
    }
    return "unknown v2 handshake error";
}

V2HandshakeResult v2_client_handshake(transport::ControlSocket& socket,
                                      const V2ClientConfig& config) {
    if (!socket || !valid_client_config(config)) {
        V2HandshakeStatus status{};
        status.error = V2HandshakeError::invalid_argument;
        return failed(status);
    }

    auto buffer_region = shm::create_file_region(
        config.buffer_path, config.buffer_size,
        shm::FileCleanup::unlink_on_destroy);
    if (!buffer_region) {
        return failed(mapping_failure(buffer_region.status));
    }
    auto buffer_pool = shm::initialize_buffer_pool(
        buffer_region.value.data(), buffer_region.value.size(),
        config.buffer_tiers, shm::BufferListRole::creator);
    if (!buffer_pool) {
        return failed(pool_failure(buffer_pool.error));
    }

    const auto manager_size = shm::queue_manager_region_size(
        config.queue_capacity, shm::native_queue_architecture());
    if (!manager_size) {
        return failed(queue_failure(shm::QueueError::invalid_capacity));
    }
    auto queue_region = shm::create_file_region(
        config.queue_path, manager_size.value,
        shm::FileCleanup::unlink_on_destroy);
    if (!queue_region) {
        return failed(mapping_failure(queue_region.status));
    }
    const auto queue_size = manager_size.value / shm::queue_count;
    auto send_queue = shm::initialize_shared_queue(
        queue_region.value.data(), queue_size, config.queue_capacity);
    if (!send_queue) {
        return failed(queue_failure(send_queue.error));
    }
    auto receive_queue = shm::initialize_shared_queue(
        queue_region.value.data() + queue_size, queue_size,
        config.queue_capacity);
    if (!receive_queue) {
        return failed(queue_failure(receive_queue.error));
    }

    const auto frame = protocol::encode_shared_memory_metadata(
        v2_protocol_version, protocol::EventType::share_memory_by_file_path,
        config.queue_path, config.buffer_path);
    if (!frame) {
        return failed(codec_failure(frame.error));
    }
    const auto written = socket.write_full(frame.value.data(), frame.value.size());
    if (!written) {
        return failed(transport_failure(written));
    }

    return {V2SharedMemory(std::move(buffer_region.value),
                           std::move(queue_region.value),
                           std::move(buffer_pool.value),
                           std::move(send_queue.value),
                           std::move(receive_queue.value), true),
            {}};
}

V2HandshakeResult v2_server_handshake(transport::ControlSocket& socket,
                                      std::uint32_t max_frame_length) {
    if (!socket || max_frame_length < protocol::header_size ||
        max_frame_length > v2_max_metadata_frame_length) {
        V2HandshakeStatus status{};
        status.error = V2HandshakeError::invalid_argument;
        return failed(status);
    }

    std::array<std::uint8_t, protocol::header_size> header_bytes{};
    const auto header_read =
        socket.read_full(header_bytes.data(), header_bytes.size());
    if (!header_read) {
        return failed(transport_failure(header_read));
    }
    const auto header = protocol::decode_header(
        header_bytes.data(), header_bytes.size(), max_frame_length);
    if (!header) {
        return failed(codec_failure(header.error));
    }
    if (header.value.version != v2_protocol_version ||
        header.value.type != protocol::EventType::share_memory_by_file_path) {
        V2HandshakeStatus status{};
        status.error = V2HandshakeError::unexpected_header;
        return failed(status);
    }

    std::vector<std::uint8_t> frame(header.value.length);
    std::copy(header_bytes.begin(), header_bytes.end(), frame.begin());
    const auto body_size = frame.size() - header_bytes.size();
    if (body_size != 0U) {
        const auto body_read =
            socket.read_full(frame.data() + header_bytes.size(), body_size);
        if (!body_read) {
            return failed(transport_failure(body_read));
        }
    }
    const auto metadata = protocol::decode_shared_memory_metadata(
        frame.data(), frame.size(), max_frame_length);
    if (!metadata) {
        return failed(codec_failure(metadata.error));
    }

    auto queue_region = shm::map_file_region(metadata.value.queue_path);
    if (!queue_region) {
        return failed(mapping_failure(queue_region.status));
    }
    if (queue_region.value.size() % shm::queue_count != 0U) {
        return failed(queue_failure(shm::QueueError::truncated_region));
    }
    const auto queue_size = queue_region.value.size() / shm::queue_count;
    auto receive_queue = shm::map_shared_queue(queue_region.value.data(),
                                               queue_size);
    if (!receive_queue) {
        return failed(queue_failure(receive_queue.error));
    }
    auto send_queue = shm::map_shared_queue(
        queue_region.value.data() + queue_size, queue_size);
    if (!send_queue) {
        return failed(queue_failure(send_queue.error));
    }

    auto buffer_region = shm::map_file_region(metadata.value.buffer_path);
    if (!buffer_region) {
        return failed(mapping_failure(buffer_region.status));
    }
    auto buffer_pool = shm::map_buffer_pool(
        buffer_region.value.data(), buffer_region.value.size(),
        shm::BufferListRole::mapper);
    if (!buffer_pool) {
        return failed(pool_failure(buffer_pool.error));
    }

    return {V2SharedMemory(std::move(buffer_region.value),
                           std::move(queue_region.value),
                           std::move(buffer_pool.value),
                           std::move(send_queue.value),
                           std::move(receive_queue.value), false),
            {}};
}

}  // namespace shmipc::core
