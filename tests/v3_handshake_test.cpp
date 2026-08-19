#include "core/v3_handshake.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct SocketPair {
    shmipc::transport::ControlSocket client{};
    shmipc::transport::ControlSocket server{};
};

shmipc::transport::TransportResult<SocketPair> make_socket_pair() {
    int descriptors[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return {{}, shmipc::transport::TransportError::system_error, errno};
    }
    auto client = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto server = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!client || !server) {
        return {{},
                shmipc::transport::TransportError::system_error,
                client ? server.system_error : client.system_error};
    }
    return {{std::move(client.value), std::move(server.value)},
            shmipc::transport::TransportError::none,
            0};
}

shmipc::core::V3ClientConfig client_config() {
    return {"queue-logical-name",
            "buffer-logical-name",
            64U,
            1U << 20U,
            {{4096U, 60U}, {8192U, 40U}}};
}

#if defined(__linux__)

std::size_t open_descriptor_count() {
    auto* directory = ::opendir("/proc/self/fd");
    if (directory == nullptr) {
        return 0U;
    }
    std::size_t count = 0U;
    while (::readdir(directory) != nullptr) {
        ++count;
    }
    static_cast<void>(::closedir(directory));
    return count;
}

bool send_header(shmipc::transport::ControlSocket& socket,
                 shmipc::protocol::EventType type) {
    const auto frame = shmipc::protocol::encode_header(
        {static_cast<std::uint32_t>(shmipc::protocol::header_size), 3U, type});
    return frame && socket.write_full(frame.value.data(), frame.value.size());
}

bool read_header(shmipc::transport::ControlSocket& socket,
                 shmipc::protocol::EventType expected) {
    std::array<std::uint8_t, shmipc::protocol::header_size> bytes{};
    const auto read = socket.read_full(bytes.data(), bytes.size());
    const auto header =
        read ? shmipc::protocol::decode_header(bytes.data(), bytes.size())
             : shmipc::protocol::HeaderResult{};
    return read && header && header.value.length == bytes.size() &&
           header.value.version == 3U && header.value.type == expected;
}

bool read_metadata(shmipc::transport::ControlSocket& socket) {
    std::array<std::uint8_t, shmipc::protocol::header_size> header_bytes{};
    const auto read =
        socket.read_full(header_bytes.data(), header_bytes.size());
    const auto header = read ? shmipc::protocol::decode_header(
                                   header_bytes.data(), header_bytes.size())
                             : shmipc::protocol::HeaderResult{};
    if (!read || !header || header.value.length < header_bytes.size() ||
        header.value.type !=
            shmipc::protocol::EventType::share_memory_by_memfd) {
        return false;
    }
    std::vector<std::uint8_t> frame(header.value.length);
    std::copy(header_bytes.begin(), header_bytes.end(), frame.begin());
    const auto body_size = frame.size() - header_bytes.size();
    const auto body =
        socket.read_full(frame.data() + header_bytes.size(), body_size);
    return body && shmipc::protocol::decode_shared_memory_metadata(
                       frame.data(), frame.size());
}

bool send_metadata(shmipc::transport::ControlSocket& socket) {
    const auto metadata = shmipc::protocol::encode_shared_memory_metadata(
        3U, shmipc::protocol::EventType::share_memory_by_memfd, "queue-name",
        "buffer-name");
    return metadata &&
           socket.write_full(metadata.value.data(), metadata.value.size());
}

bool test_success_and_resource_direction() {
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    auto server_future = std::async(std::launch::async, [&] {
        return shmipc::core::v3_server_handshake(sockets.value.server);
    });
    auto client = shmipc::core::v3_client_handshake(sockets.value.client,
                                                    client_config());
    auto server = server_future.get();
    if (!client || !server || !client.value.is_creator() ||
        server.value.is_creator() ||
        client.value.queue_name() != server.value.queue_name() ||
        client.value.buffer_name() != server.value.buffer_name() ||
        client.value.queue_fd() < 0 || client.value.buffer_fd() < 0 ||
        server.value.queue_fd() < 0 || server.value.buffer_fd() < 0 ||
        client.value.queue_fd() == server.value.queue_fd() ||
        client.value.buffer_fd() == server.value.buffer_fd()) {
        return false;
    }

    const shmipc::shm::QueueElement request{7U, 11U, 1U};
    const shmipc::shm::QueueElement response{8U, 12U, 2U};
    if (client.value.send_queue().put(request) !=
            shmipc::shm::QueueError::none ||
        server.value.send_queue().put(response) !=
            shmipc::shm::QueueError::none) {
        return false;
    }
    const auto received_request = server.value.receive_queue().pop();
    const auto received_response = client.value.receive_queue().pop();
    return received_request && received_response &&
           received_request.value.sequence_id == request.sequence_id &&
           received_request.value.buffer_offset == request.buffer_offset &&
           received_response.value.sequence_id == response.sequence_id &&
           received_response.value.buffer_offset == response.buffer_offset &&
           client.value.buffer_pool().list_count() == 2U &&
           server.value.buffer_pool().list_count() == 2U;
}

bool test_client_rejects_wrong_ready_ack() {
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    auto peer = std::async(std::launch::async, [&] {
        const auto negotiated = shmipc::core::negotiate_protocol_version_server(
            sockets.value.server);
        return negotiated && read_metadata(sockets.value.server) &&
               send_header(sockets.value.server,
                           shmipc::protocol::EventType::ack_share_memory);
    });
    const auto client = shmipc::core::v3_client_handshake(sockets.value.client,
                                                          client_config());
    return peer.get() && !client &&
           client.status.error ==
               shmipc::core::V3HandshakeError::unexpected_header;
}

bool test_client_rejects_wrong_final_ack() {
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    auto peer = std::async(std::launch::async, [&] {
        const auto negotiated = shmipc::core::negotiate_protocol_version_server(
            sockets.value.server);
        if (!negotiated || !read_metadata(sockets.value.server) ||
            !send_header(sockets.value.server,
                         shmipc::protocol::EventType::ack_ready_receive_fd)) {
            return false;
        }
        auto descriptors = sockets.value.server.receive_file_descriptors(2U);
        return descriptors && descriptors.value.size() == 2U &&
               send_header(sockets.value.server,
                           shmipc::protocol::EventType::ack_ready_receive_fd);
    });
    const auto client = shmipc::core::v3_client_handshake(sockets.value.client,
                                                          client_config());
    return peer.get() && !client &&
           client.status.error ==
               shmipc::core::V3HandshakeError::unexpected_header;
}

bool test_server_rejects_descriptor_count(std::size_t count) {
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    auto server_future = std::async(std::launch::async, [&] {
        return shmipc::core::v3_server_handshake(sockets.value.server);
    });
    const auto negotiated =
        shmipc::core::negotiate_protocol_version_client(sockets.value.client);
    if (!negotiated || !send_metadata(sockets.value.client) ||
        !read_header(sockets.value.client,
                     shmipc::protocol::EventType::ack_ready_receive_fd)) {
        return false;
    }
    int pipe_descriptors[2]{-1, -1};
    if (::pipe(pipe_descriptors) != 0) {
        return false;
    }
    const std::array<int, 3> descriptors{
        {pipe_descriptors[0], pipe_descriptors[0], pipe_descriptors[0]}};
    const auto sent =
        sockets.value.client.send_file_descriptors(descriptors.data(), count);
    const auto server = server_future.get();
    static_cast<void>(::close(pipe_descriptors[0]));
    static_cast<void>(::close(pipe_descriptors[1]));
    if (!sent || server) {
        return false;
    }
    return count == 1U
               ? server.status.error ==
                     shmipc::core::V3HandshakeError::descriptor_count_error
               : server.status.error ==
                         shmipc::core::V3HandshakeError::transport_error &&
                     server.status.transport_error ==
                         shmipc::transport::TransportError::buffer_limit;
}

bool test_server_rejects_truncated_metadata() {
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    auto server_future = std::async(std::launch::async, [&] {
        return shmipc::core::v3_server_handshake(sockets.value.server);
    });
    const auto negotiated =
        shmipc::core::negotiate_protocol_version_client(sockets.value.client);
    const auto header = shmipc::protocol::encode_header(
        {12U, 3U, shmipc::protocol::EventType::share_memory_by_memfd});
    const std::uint8_t partial = 0U;
    if (!negotiated || !header ||
        !sockets.value.client.write_full(header.value.data(),
                                         header.value.size()) ||
        !sockets.value.client.write_full(&partial, 1U)) {
        return false;
    }
    static_cast<void>(sockets.value.client.shutdown());
    const auto server = server_future.get();
    return !server &&
           server.status.error ==
               shmipc::core::V3HandshakeError::transport_error &&
           server.status.transport_error ==
               shmipc::transport::TransportError::end_of_stream;
}

#endif

bool test_platform_and_invalid_arguments() {
    shmipc::transport::ControlSocket invalid{};
    const auto bad_client =
        shmipc::core::v3_client_handshake(invalid, client_config());
    const auto bad_server = shmipc::core::v3_server_handshake(invalid);
    if (bad_client.status.error !=
            shmipc::core::V3HandshakeError::invalid_argument ||
        bad_server.status.error !=
            shmipc::core::V3HandshakeError::invalid_argument) {
        return false;
    }
#if defined(__linux__)
    return true;
#else
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    const auto unsupported_client = shmipc::core::v3_client_handshake(
        sockets.value.client, client_config());
    const auto unsupported_server =
        shmipc::core::v3_server_handshake(sockets.value.server);
    return unsupported_client.status.error ==
               shmipc::core::V3HandshakeError::unsupported &&
           unsupported_server.status.error ==
               shmipc::core::V3HandshakeError::unsupported;
#endif
}

} // namespace

int main() {
    bool ok = test_platform_and_invalid_arguments();
#if defined(__linux__)
    const auto descriptors_before_failures = open_descriptor_count();
    ok = ok && test_success_and_resource_direction() &&
         test_client_rejects_wrong_ready_ack() &&
         test_client_rejects_wrong_final_ack() &&
         test_server_rejects_descriptor_count(1U) &&
         test_server_rejects_descriptor_count(3U) &&
         test_server_rejects_truncated_metadata() &&
         open_descriptor_count() == descriptors_before_failures;
#endif
    if (!ok) {
        std::cerr << "v3 handshake test failed\n";
    }
    return ok ? 0 : 1;
}
