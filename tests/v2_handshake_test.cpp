#include "core/v2_handshake.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using shmipc::core::V2HandshakeError;
using shmipc::core::V2HandshakeResult;
using shmipc::transport::TransportError;

struct SocketPair {
    shmipc::transport::ControlSocket client{};
    shmipc::transport::ControlSocket server{};
};

shmipc::transport::TransportResult<SocketPair> make_socket_pair() {
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return {{}, TransportError::system_error, errno};
    }
    auto client = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto server = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!client || !server) {
        return {{}, TransportError::system_error,
                client ? server.system_error : client.system_error};
    }
    return {{std::move(client.value), std::move(server.value)},
            TransportError::none, 0};
}

struct TestDirectory {
    std::array<char, 64> storage{};
    std::string path{};

    bool create() {
        const std::string pattern = "/tmp/shmipc-v2-test.XXXXXX";
        std::copy(pattern.begin(), pattern.end(), storage.begin());
        auto* const directory = ::mkdtemp(storage.data());
        if (directory == nullptr) {
            return false;
        }
        path = directory;
        return true;
    }

    ~TestDirectory() {
        if (!path.empty()) {
            static_cast<void>(::rmdir(path.c_str()));
        }
    }
};

shmipc::core::V2ClientConfig config_for(const TestDirectory& directory) {
    return {directory.path + "/queue", directory.path + "/buffer", 64U,
            1U << 20U, {{4096U, 60U}, {8192U, 40U}}};
}

bool test_success_and_orientation() {
    TestDirectory directory;
    auto sockets = make_socket_pair();
    if (!directory.create() || !sockets) {
        return false;
    }
    const auto config = config_for(directory);
    bool exchanged = false;
    {
        std::promise<V2HandshakeResult> server_promise;
        auto server_future = server_promise.get_future();
        std::thread server_thread([&] {
            server_promise.set_value(
                shmipc::core::v2_server_handshake(sockets.value.server));
        });
        auto client =
            shmipc::core::v2_client_handshake(sockets.value.client, config);
        auto server = server_future.get();
        server_thread.join();
        if (!client || !server || !client.value.is_creator() ||
            server.value.is_creator() ||
            client.value.queue_path() != config.queue_path ||
            server.value.buffer_path() != config.buffer_path ||
            ::access(config.queue_path.c_str(), F_OK) != 0 ||
            ::access(config.buffer_path.c_str(), F_OK) != 0) {
            return false;
        }

        const shmipc::shm::QueueElement client_element{11U, 22U, 33U};
        const shmipc::shm::QueueElement server_element{44U, 55U, 66U};
        if (client.value.send_queue().put(client_element) !=
                shmipc::shm::QueueError::none ||
            server.value.send_queue().put(server_element) !=
                shmipc::shm::QueueError::none) {
            return false;
        }
        const auto server_received = server.value.receive_queue().pop();
        const auto client_received = client.value.receive_queue().pop();
        if (!server_received || !client_received ||
            server_received.value.sequence_id != client_element.sequence_id ||
            server_received.value.buffer_offset != client_element.buffer_offset ||
            server_received.value.status != client_element.status ||
            client_received.value.sequence_id != server_element.sequence_id ||
            client_received.value.buffer_offset != server_element.buffer_offset ||
            client_received.value.status != server_element.status) {
            return false;
        }

        auto client_buffer = client.value.buffer_pool().allocate(16U);
        auto server_buffer = server.value.buffer_pool().allocate(16U);
        if (!client_buffer || !server_buffer ||
            client.value.buffer_pool().recycle(std::move(client_buffer.value)) !=
                shmipc::shm::BufferPoolError::none ||
            server.value.buffer_pool().recycle(std::move(server_buffer.value)) !=
                shmipc::shm::BufferPoolError::none) {
            return false;
        }
        exchanged = true;
    }
    return exchanged && ::access(config.queue_path.c_str(), F_OK) != 0 &&
           ::access(config.buffer_path.c_str(), F_OK) != 0;
}

bool test_invalid_and_unexpected_inputs() {
    auto sockets = make_socket_pair();
    if (!sockets) {
        return false;
    }
    shmipc::core::V2ClientConfig invalid{};
    const auto invalid_client =
        shmipc::core::v2_client_handshake(sockets.value.client, invalid);
    const auto invalid_server = shmipc::core::v2_server_handshake(
        sockets.value.server, static_cast<std::uint32_t>(
                                  shmipc::protocol::header_size - 1U));
    if (invalid_client.status.error != V2HandshakeError::invalid_argument ||
        invalid_server.status.error != V2HandshakeError::invalid_argument) {
        return false;
    }

    auto wrong_sockets = make_socket_pair();
    const auto wrong_header = shmipc::protocol::encode_header(
        {static_cast<std::uint32_t>(shmipc::protocol::header_size), 2U,
         shmipc::protocol::EventType::polling});
    if (!wrong_sockets || !wrong_header ||
        !wrong_sockets.value.client.write_full(wrong_header.value.data(),
                                               wrong_header.value.size())) {
        return false;
    }
    const auto wrong =
        shmipc::core::v2_server_handshake(wrong_sockets.value.server);
    if (wrong.status.error != V2HandshakeError::unexpected_header) {
        return false;
    }

    auto wrong_version_sockets = make_socket_pair();
    const auto wrong_version_header = shmipc::protocol::encode_header(
        {static_cast<std::uint32_t>(shmipc::protocol::header_size), 3U,
         shmipc::protocol::EventType::share_memory_by_file_path});
    if (!wrong_version_sockets || !wrong_version_header ||
        !wrong_version_sockets.value.client.write_full(
            wrong_version_header.value.data(),
            wrong_version_header.value.size())) {
        return false;
    }
    const auto wrong_version = shmipc::core::v2_server_handshake(
        wrong_version_sockets.value.server);
    if (wrong_version.status.error != V2HandshakeError::unexpected_header) {
        return false;
    }

    auto truncated_sockets = make_socket_pair();
    const auto truncated_header = shmipc::protocol::encode_header(
        {12U, 2U, shmipc::protocol::EventType::share_memory_by_file_path});
    if (!truncated_sockets || !truncated_header ||
        !truncated_sockets.value.client.write_full(
            truncated_header.value.data(), truncated_header.value.size())) {
        return false;
    }
    static_cast<void>(truncated_sockets.value.client.close());
    const auto truncated =
        shmipc::core::v2_server_handshake(truncated_sockets.value.server);
    return truncated.status.error == V2HandshakeError::transport_error &&
           truncated.status.transport_error == TransportError::end_of_stream;
}

bool test_server_mapping_failure() {
    TestDirectory directory;
    auto sockets = make_socket_pair();
    if (!directory.create() || !sockets) {
        return false;
    }
    const auto frame = shmipc::protocol::encode_shared_memory_metadata(
        shmipc::core::v2_protocol_version,
        shmipc::protocol::EventType::share_memory_by_file_path,
        directory.path + "/missing-queue",
        directory.path + "/missing-buffer");
    if (!frame || !sockets.value.client.write_full(frame.value.data(),
                                                   frame.value.size())) {
        return false;
    }
    const auto result =
        shmipc::core::v2_server_handshake(sockets.value.server);
    return result.status.error == V2HandshakeError::mapping_error &&
           result.status.mapping_error ==
               shmipc::shm::MappingError::open_failed;
}

bool test_failure_cleanup_and_existing_file() {
    TestDirectory directory;
    auto sockets = make_socket_pair();
    if (!directory.create() || !sockets) {
        return false;
    }
    const auto config = config_for(directory);
    const auto descriptor =
        ::open(config.buffer_path.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) {
        return false;
    }
    static_cast<void>(::close(descriptor));
    const auto duplicate =
        shmipc::core::v2_client_handshake(sockets.value.client, config);
    const auto preserved = ::access(config.buffer_path.c_str(), F_OK) == 0;
    static_cast<void>(::unlink(config.buffer_path.c_str()));
    if (duplicate.status.error != V2HandshakeError::mapping_error ||
        duplicate.status.mapping_error != shmipc::shm::MappingError::open_failed ||
        !preserved) {
        return false;
    }

    auto closed_sockets = make_socket_pair();
    if (!closed_sockets) {
        return false;
    }
    static_cast<void>(closed_sockets.value.server.close());
    const auto failed_write = shmipc::core::v2_client_handshake(
        closed_sockets.value.client, config);
    return failed_write.status.error == V2HandshakeError::transport_error &&
           ::access(config.queue_path.c_str(), F_OK) != 0 &&
           ::access(config.buffer_path.c_str(), F_OK) != 0;
}

}  // namespace

int main() {
    if (!test_success_and_orientation()) {
        std::cerr << "v2 handshake success/orientation test failed\n";
        return 1;
    }
    if (!test_invalid_and_unexpected_inputs()) {
        std::cerr << "v2 handshake invalid-input test failed\n";
        return 1;
    }
    if (!test_failure_cleanup_and_existing_file()) {
        std::cerr << "v2 handshake cleanup test failed\n";
        return 1;
    }
    if (!test_server_mapping_failure()) {
        std::cerr << "v2 handshake mapping-failure test failed\n";
        return 1;
    }
    return 0;
}
