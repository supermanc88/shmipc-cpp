#include "core/v2_client_session.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

struct SocketPair {
    shmipc::transport::ControlSocket client{};
    shmipc::transport::ControlSocket server{};
};

shmipc::transport::TransportResult<SocketPair> make_socket_pair() {
    int descriptors[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return {{}, shmipc::transport::TransportError::system_error, errno};
    }
    auto client = shmipc::transport::adopt_control_socket(descriptors[0]);
    auto server = shmipc::transport::adopt_control_socket(descriptors[1]);
    if (!client || !server) {
        return {{}, shmipc::transport::TransportError::system_error,
                client ? server.system_error : client.system_error};
    }
    return {{std::move(client.value), std::move(server.value)},
            shmipc::transport::TransportError::none, 0};
}

struct TestDirectory {
    std::array<char, 64> storage{};
    std::string path{};

    bool create() {
        const std::string pattern = "/tmp/shmipc-v2-session.XXXXXX";
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

bool read_polling(shmipc::transport::ControlSocket& socket) {
    std::array<std::uint8_t, shmipc::protocol::header_size> bytes{};
    const auto read = socket.read_full(bytes.data(), bytes.size());
    if (!read) {
        return false;
    }
    const auto header = shmipc::protocol::decode_header(bytes.data(),
                                                        bytes.size());
    return header && header.value.version == 2U &&
           header.value.length == shmipc::protocol::header_size &&
           header.value.type == shmipc::protocol::EventType::polling;
}

bool send_polling(shmipc::transport::ControlSocket& socket) {
    const auto frame = shmipc::protocol::encode_header(
        {static_cast<std::uint32_t>(shmipc::protocol::header_size), 2U,
         shmipc::protocol::EventType::polling});
    return frame &&
           static_cast<bool>(socket.write_full(frame.value.data(),
                                               frame.value.size()));
}

bool run_server(shmipc::transport::ControlSocket& socket,
                const std::vector<std::uint8_t>& request,
                const std::vector<std::uint8_t>& response) {
    auto handshake = shmipc::core::v2_server_handshake(socket);
    if (!handshake || !read_polling(socket)) {
        return false;
    }
    const auto incoming = handshake.value.receive_queue().pop();
    if (!incoming ||
        incoming.value.sequence_id != shmipc::core::v2_single_stream_id ||
        incoming.value.status != 0U) {
        return false;
    }
    auto chain = handshake.value.buffer_pool().adopt_chain(
        incoming.value.buffer_offset);
    if (!chain) {
        return false;
    }
    auto reader = shmipc::shm::make_buffer_reader(
        handshake.value.buffer_pool(), std::move(chain.value));
    if (!reader) {
        return false;
    }
    const auto view = reader.value.read_bytes(request.size());
    if (!view || view.value.size() != request.size() ||
        !std::equal(request.begin(), request.end(), view.value.data()) ||
        !handshake.value.receive_queue().mark_not_working()) {
        return false;
    }

    shmipc::shm::BufferWriter writer(handshake.value.buffer_pool());
    const auto written = writer.write_bytes(response.data(), response.size());
    const auto published = written ? writer.publish()
                                   : shmipc::shm::PublishedBufferChainResult{};
    if (!written || !published ||
        handshake.value.send_queue().put(
            {shmipc::core::v2_single_stream_id,
             published.value.root_offset, 0U}) !=
            shmipc::shm::QueueError::none ||
        !handshake.value.send_queue().mark_working() ||
        !send_polling(socket)) {
        return false;
    }

    if (!read_polling(socket)) {
        return false;
    }
    const auto closed = handshake.value.receive_queue().pop();
    if (!closed ||
        closed.value.sequence_id != shmipc::core::v2_single_stream_id ||
        closed.value.status != 1U) {
        return false;
    }
    static_cast<void>(handshake.value.receive_queue().mark_not_working());
    if (handshake.value.send_queue().put(
            {shmipc::core::v2_single_stream_id, 0U, 1U}) !=
            shmipc::shm::QueueError::none ||
        !handshake.value.send_queue().mark_working()) {
        return false;
    }
    return send_polling(socket);
}

bool test_single_stream_round_trip() {
    TestDirectory directory;
    auto sockets = make_socket_pair();
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!dispatcher) {
        return dispatcher.error == shmipc::transport::TransportError::unsupported;
    }
    if (!directory.create() || !sockets) {
        return false;
    }
    std::vector<std::uint8_t> request(20000U, 0x3cU);
    std::vector<std::uint8_t> response(17000U, 0x6bU);
    const auto config = config_for(directory);
    std::promise<bool> server_promise;
    auto server_future = server_promise.get_future();
    std::thread server_thread([&] {
        server_promise.set_value(run_server(sockets.value.server, request,
                                            response));
    });

    auto session = shmipc::core::start_v2_client_session(
        std::move(sockets.value.client), config, dispatcher.value);
    bool passed = false;
    if (session &&
        session.value.receive(1ms).status.error ==
            shmipc::core::V2SessionError::timeout &&
        session.value.send(request)) {
        auto received = session.value.receive(5s);
        if (received && received.value == response &&
            session.value.close_stream() &&
            session.value.wait_remote_close(5s)) {
            passed = true;
        }
    }
    static_cast<void>(session.value.close());
    server_thread.join();
    const auto server_passed = server_future.get();
    static_cast<void>(dispatcher.value.stop());
    return passed && server_passed &&
           ::access(config.queue_path.c_str(), F_OK) != 0 &&
           ::access(config.buffer_path.c_str(), F_OK) != 0;
}

bool test_invalid_and_timeout() {
    shmipc::core::V2ClientSession empty;
    const std::vector<std::uint8_t> payload{1U};
    return empty.send(payload).error ==
               shmipc::core::V2SessionError::invalid_argument &&
           empty.receive(1ms).status.error ==
               shmipc::core::V2SessionError::invalid_argument &&
           empty.wait_remote_close(1ms).error ==
               shmipc::core::V2SessionError::invalid_argument;
}

}  // namespace

int main() {
    if (!test_single_stream_round_trip()) {
        std::cerr << "v2 single-stream round-trip test failed\n";
        return 1;
    }
    if (!test_invalid_and_timeout()) {
        std::cerr << "v2 session invalid-input test failed\n";
        return 1;
    }
    return 0;
}
