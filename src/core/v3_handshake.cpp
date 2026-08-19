#include "core/v3_handshake.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace shmipc::core {
namespace {

V3HandshakeResult failed(V3HandshakeStatus status) { return {{}, status}; }

V3HandshakeStatus simple_failure(V3HandshakeError error) {
    V3HandshakeStatus status{};
    status.error = error;
    return status;
}

V3HandshakeStatus
negotiation_failure(const ProtocolVersionNegotiationStatus& result) {
    V3HandshakeStatus status{};
    status.error = V3HandshakeError::version_negotiation_error;
    status.system_error = result.system_error;
    status.negotiation_status = result;
    status.transport_error = result.transport_error;
    status.codec_error = result.codec_error;
    return status;
}

V3HandshakeStatus transport_failure(transport::TransportError error,
                                    int system_error) {
    V3HandshakeStatus status{};
    status.error = V3HandshakeError::transport_error;
    status.system_error = system_error;
    status.transport_error = error;
    return status;
}

V3HandshakeStatus transport_failure(const transport::IoResult& result) {
    return transport_failure(result.error, result.system_error);
}

V3HandshakeStatus codec_failure(protocol::CodecError error) {
    V3HandshakeStatus status{};
    status.error = V3HandshakeError::codec_error;
    status.codec_error = error;
    return status;
}

V3HandshakeStatus mapping_failure(const shm::MappingStatus& result) {
    V3HandshakeStatus status{};
    status.error = result.error == shm::MappingError::unsupported
                       ? V3HandshakeError::unsupported
                       : V3HandshakeError::mapping_error;
    status.system_error = result.system_error;
    status.mapping_error = result.error;
    return status;
}

V3HandshakeStatus pool_failure(shm::BufferPoolError error) {
    V3HandshakeStatus status{};
    status.error = V3HandshakeError::buffer_pool_error;
    status.buffer_pool_error = error;
    return status;
}

V3HandshakeStatus queue_failure(shm::QueueError error) {
    V3HandshakeStatus status{};
    status.error = V3HandshakeError::queue_error;
    status.queue_error = error;
    return status;
}

bool valid_client_config(const V3ClientConfig& config) {
    return !config.queue_name.empty() && !config.buffer_name.empty() &&
           config.queue_name != config.buffer_name &&
           config.queue_capacity != 0U && config.buffer_size != 0U &&
           config.buffer_size <= std::numeric_limits<std::uint32_t>::max() &&
           !config.buffer_tiers.empty();
}

#if defined(__linux__)
V3HandshakeStatus send_header(transport::ControlSocket& socket,
                              protocol::EventType type) {
    const auto encoded = protocol::encode_header(
        {static_cast<std::uint32_t>(protocol::header_size), v3_protocol_version,
         type});
    if (!encoded) {
        return codec_failure(encoded.error);
    }
    const auto written =
        socket.write_full(encoded.value.data(), encoded.value.size());
    return written ? V3HandshakeStatus{} : transport_failure(written);
}
#endif

V3HandshakeStatus wait_header(transport::ControlSocket& socket,
                              protocol::EventType expected) {
    std::array<std::uint8_t, protocol::header_size> bytes{};
    const auto read = socket.read_full(bytes.data(), bytes.size());
    if (!read) {
        return transport_failure(read);
    }
    const auto header = protocol::decode_header(bytes.data(), bytes.size());
    if (!header) {
        return codec_failure(header.error);
    }
    if (header.value.length != protocol::header_size ||
        header.value.version != v3_protocol_version ||
        header.value.type != expected) {
        return simple_failure(V3HandshakeError::unexpected_header);
    }
    return {};
}

} // namespace

V3SharedMemory::V3SharedMemory(shm::SharedMemoryRegion&& buffer_region,
                               shm::SharedMemoryRegion&& queue_region,
                               shm::BufferPool&& buffer_pool,
                               shm::SharedQueue&& send_queue,
                               shm::SharedQueue&& receive_queue,
                               std::string queue_name, std::string buffer_name,
                               bool creator) noexcept
    : buffer_region_(std::move(buffer_region)),
      queue_region_(std::move(queue_region)),
      buffer_pool_(std::move(buffer_pool)), send_queue_(std::move(send_queue)),
      receive_queue_(std::move(receive_queue)),
      queue_name_(std::move(queue_name)), buffer_name_(std::move(buffer_name)),
      creator_(creator) {}

V3SharedMemory::operator bool() const noexcept {
    return buffer_region_ && queue_region_ && buffer_pool_ && send_queue_ &&
           receive_queue_;
}

bool V3SharedMemory::is_creator() const noexcept { return creator_; }

const std::string& V3SharedMemory::queue_name() const noexcept {
    return queue_name_;
}

const std::string& V3SharedMemory::buffer_name() const noexcept {
    return buffer_name_;
}

int V3SharedMemory::queue_fd() const noexcept { return queue_region_.fd(); }

int V3SharedMemory::buffer_fd() const noexcept { return buffer_region_.fd(); }

shm::BufferPool& V3SharedMemory::buffer_pool() noexcept { return buffer_pool_; }

shm::SharedQueue& V3SharedMemory::send_queue() noexcept { return send_queue_; }

shm::SharedQueue& V3SharedMemory::receive_queue() noexcept {
    return receive_queue_;
}

const char* to_string(V3HandshakeError error) noexcept {
    switch (error) {
        case V3HandshakeError::none:
            return "none";
        case V3HandshakeError::invalid_argument:
            return "invalid argument";
        case V3HandshakeError::unsupported:
            return "unsupported";
        case V3HandshakeError::version_negotiation_error:
            return "version negotiation error";
        case V3HandshakeError::unsupported_version:
            return "unsupported version";
        case V3HandshakeError::transport_error:
            return "transport error";
        case V3HandshakeError::codec_error:
            return "codec error";
        case V3HandshakeError::unexpected_header:
            return "unexpected header";
        case V3HandshakeError::descriptor_count_error:
            return "descriptor count error";
        case V3HandshakeError::mapping_error:
            return "mapping error";
        case V3HandshakeError::buffer_pool_error:
            return "buffer pool error";
        case V3HandshakeError::queue_error:
            return "queue error";
    }
    return "unknown v3 handshake error";
}

V3HandshakeResult v3_client_handshake(transport::ControlSocket& socket,
                                      const V3ClientConfig& config) {
    if (!socket || !valid_client_config(config)) {
        return failed(simple_failure(V3HandshakeError::invalid_argument));
    }

    auto buffer_region =
        shm::create_memfd_region(config.buffer_name, config.buffer_size);
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
    auto queue_region =
        shm::create_memfd_region(config.queue_name, manager_size.value);
    if (!queue_region) {
        return failed(mapping_failure(queue_region.status));
    }
    const auto queue_size = manager_size.value / shm::queue_count;
    auto send_queue = shm::initialize_shared_queue(
        queue_region.value.data(), queue_size, config.queue_capacity);
    if (!send_queue) {
        return failed(queue_failure(send_queue.error));
    }
    auto receive_queue =
        shm::initialize_shared_queue(queue_region.value.data() + queue_size,
                                     queue_size, config.queue_capacity);
    if (!receive_queue) {
        return failed(queue_failure(receive_queue.error));
    }

    const auto negotiation = negotiate_protocol_version_client(socket);
    if (!negotiation) {
        return failed(negotiation_failure(negotiation.status));
    }
    if (negotiation.negotiated_version != v3_protocol_version) {
        return failed(simple_failure(V3HandshakeError::unsupported_version));
    }

    const auto metadata = protocol::encode_shared_memory_metadata(
        v3_protocol_version, protocol::EventType::share_memory_by_memfd,
        config.queue_name, config.buffer_name);
    if (!metadata) {
        return failed(codec_failure(metadata.error));
    }
    const auto metadata_write =
        socket.write_full(metadata.value.data(), metadata.value.size());
    if (!metadata_write) {
        return failed(transport_failure(metadata_write));
    }
    const auto ready =
        wait_header(socket, protocol::EventType::ack_ready_receive_fd);
    if (!ready) {
        return failed(ready);
    }

    const std::array<int, 2> descriptors{
        {buffer_region.value.fd(), queue_region.value.fd()}};
    const auto descriptor_send =
        socket.send_file_descriptors(descriptors.data(), descriptors.size());
    if (!descriptor_send) {
        return failed(transport_failure(descriptor_send));
    }
    const auto acknowledged =
        wait_header(socket, protocol::EventType::ack_share_memory);
    if (!acknowledged) {
        return failed(acknowledged);
    }

    return {V3SharedMemory(
                std::move(buffer_region.value), std::move(queue_region.value),
                std::move(buffer_pool.value), std::move(send_queue.value),
                std::move(receive_queue.value), config.queue_name,
                config.buffer_name, true),
            {}};
}

V3HandshakeResult v3_server_handshake(transport::ControlSocket& socket,
                                      std::uint32_t max_frame_length) {
    if (!socket || max_frame_length < protocol::header_size ||
        max_frame_length > v3_max_metadata_frame_length) {
        return failed(simple_failure(V3HandshakeError::invalid_argument));
    }
#if !defined(__linux__)
    return failed(simple_failure(V3HandshakeError::unsupported));
#else
    const auto negotiation = negotiate_protocol_version_server(socket);
    if (!negotiation) {
        return failed(negotiation_failure(negotiation.status));
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
    if (header.value.version != v3_protocol_version ||
        header.value.type != protocol::EventType::share_memory_by_memfd) {
        return failed(simple_failure(V3HandshakeError::unexpected_header));
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

    const auto ready =
        send_header(socket, protocol::EventType::ack_ready_receive_fd);
    if (!ready) {
        return failed(ready);
    }
    auto descriptors = socket.receive_file_descriptors(2U);
    if (!descriptors) {
        return failed(
            transport_failure(descriptors.error, descriptors.system_error));
    }
    if (descriptors.value.size() != 2U) {
        return failed(simple_failure(V3HandshakeError::descriptor_count_error));
    }

    auto queue_region = shm::map_memfd_region(descriptors.value.release(1U),
                                              shm::FdOwnership::transferred);
    if (!queue_region) {
        return failed(mapping_failure(queue_region.status));
    }
    if (queue_region.value.size() % shm::queue_count != 0U) {
        return failed(queue_failure(shm::QueueError::truncated_region));
    }
    const auto queue_size = queue_region.value.size() / shm::queue_count;
    auto receive_queue =
        shm::map_shared_queue(queue_region.value.data(), queue_size);
    if (!receive_queue) {
        return failed(queue_failure(receive_queue.error));
    }
    auto send_queue = shm::map_shared_queue(
        queue_region.value.data() + queue_size, queue_size);
    if (!send_queue) {
        return failed(queue_failure(send_queue.error));
    }

    auto buffer_region = shm::map_memfd_region(descriptors.value.release(0U),
                                               shm::FdOwnership::transferred);
    if (!buffer_region) {
        return failed(mapping_failure(buffer_region.status));
    }
    auto buffer_pool = shm::map_buffer_pool(buffer_region.value.data(),
                                            buffer_region.value.size(),
                                            shm::BufferListRole::mapper);
    if (!buffer_pool) {
        return failed(pool_failure(buffer_pool.error));
    }

    const auto acknowledged =
        send_header(socket, protocol::EventType::ack_share_memory);
    if (!acknowledged) {
        return failed(acknowledged);
    }
    return {V3SharedMemory(
                std::move(buffer_region.value), std::move(queue_region.value),
                std::move(buffer_pool.value), std::move(send_queue.value),
                std::move(receive_queue.value), metadata.value.queue_path,
                metadata.value.buffer_path, false),
            {}};
#endif
}

} // namespace shmipc::core
