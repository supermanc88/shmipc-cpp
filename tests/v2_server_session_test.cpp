#include "core/v2_server_session.hpp"

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
constexpr std::uint32_t go_first_stream_id = 2U;

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
        return {{},
                shmipc::transport::TransportError::system_error,
                client ? server.system_error : client.system_error};
    }
    return {{std::move(client.value), std::move(server.value)},
            shmipc::transport::TransportError::none,
            0};
}

struct TestDirectory {
    std::array<char, 64> storage{};
    std::string path{};

    bool create() {
        const std::string pattern = "/tmp/shmipc-v2-server.XXXXXX";
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

bool send_polling(shmipc::transport::ControlSocket& socket) {
    const auto frame = shmipc::protocol::encode_header(
        {static_cast<std::uint32_t>(shmipc::protocol::header_size), 2U,
         shmipc::protocol::EventType::polling});
    return frame && socket.write_full(frame.value.data(), frame.value.size());
}

bool read_polling(shmipc::transport::ControlSocket& socket) {
    std::array<std::uint8_t, shmipc::protocol::header_size> bytes{};
    const auto read = socket.read_full(bytes.data(), bytes.size());
    if (!read) {
        return false;
    }
    const auto header =
        shmipc::protocol::decode_header(bytes.data(), bytes.size());
    return header && header.value.version == 2U &&
           header.value.type == shmipc::protocol::EventType::polling;
}

bool enqueue(shmipc::core::V2SharedMemory& memory, std::uint32_t stream_id,
             const std::vector<std::uint8_t>& payload) {
    shmipc::shm::BufferWriter writer(memory.buffer_pool());
    const auto written = writer.write_bytes(payload.data(), payload.size());
    const auto published =
        written ? writer.publish() : shmipc::shm::PublishedBufferChainResult{};
    return written && published &&
           memory.send_queue().put({stream_id, published.value.root_offset,
                                    0U}) == shmipc::shm::QueueError::none;
}

bool verify_received(shmipc::core::V2SharedMemory& memory,
                     const std::vector<std::vector<std::uint8_t>>& expected) {
    for (const auto& payload : expected) {
        const auto element = memory.receive_queue().pop();
        if (!element || element.value.sequence_id != go_first_stream_id ||
            element.value.status != 0U) {
            return false;
        }
        auto chain =
            memory.buffer_pool().adopt_chain(element.value.buffer_offset);
        if (!chain) {
            return false;
        }
        auto reader = shmipc::shm::make_buffer_reader(memory.buffer_pool(),
                                                      std::move(chain.value));
        if (!reader) {
            return false;
        }
        const auto bytes = reader.value.read_bytes(payload.size());
        if (!bytes || bytes.value.size() != payload.size() ||
            !std::equal(payload.begin(), payload.end(), bytes.value.data())) {
            return false;
        }
    }
    return memory.receive_queue().mark_not_working();
}

bool test_multi_message_round_trip() {
    auto sockets = make_socket_pair();
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    TestDirectory directory;
    if (!dispatcher) {
        return dispatcher.error ==
               shmipc::transport::TransportError::unsupported;
    }
    if (!sockets || !directory.create()) {
        return false;
    }
    const shmipc::core::V2ClientConfig config{directory.path + "/queue",
                                              directory.path + "/buffer",
                                              64U,
                                              1U << 20U,
                                              {{4096U, 60U}, {8192U, 40U}}};
    std::promise<shmipc::core::V2ServerSessionResult> promise;
    auto future = promise.get_future();
    std::thread server_thread([&] {
        promise.set_value(shmipc::core::start_v2_server_session(
            std::move(sockets.value.server), dispatcher.value));
    });
    {
        auto client =
            shmipc::core::v2_client_handshake(sockets.value.client, config);
        auto server = future.get();
        server_thread.join();
        if (!client || !server ||
            server.value.wait_stream(1ms).error !=
                shmipc::core::V2SessionError::timeout) {
            return false;
        }

        const std::vector<std::vector<std::uint8_t>> requests{
            std::vector<std::uint8_t>(31U, 0x11U),
            std::vector<std::uint8_t>(20000U, 0x22U),
            std::vector<std::uint8_t>(7000U, 0x33U)};
        for (const auto& request : requests) {
            if (!enqueue(client.value, go_first_stream_id, request)) {
                return false;
            }
        }
        if (!client.value.send_queue().mark_working() ||
            !send_polling(sockets.value.client) ||
            !server.value.wait_stream(5s) ||
            server.value.stream_id() != go_first_stream_id) {
            return false;
        }
        for (const auto& request : requests) {
            auto message = server.value.receive(5s);
            if (!message || message.value != request) {
                return false;
            }
        }

        const std::vector<std::vector<std::uint8_t>> responses{
            std::vector<std::uint8_t>(47U, 0x44U),
            std::vector<std::uint8_t>(17000U, 0x55U),
            std::vector<std::uint8_t>(9000U, 0x66U)};
        for (const auto& response : responses) {
            if (!server.value.send(response)) {
                return false;
            }
        }
        if (!read_polling(sockets.value.client) ||
            !verify_received(client.value, responses) ||
            !server.value.close_stream() ||
            !read_polling(sockets.value.client)) {
            return false;
        }
        const auto closed = client.value.receive_queue().pop();
        if (!closed || closed.value.sequence_id != go_first_stream_id ||
            closed.value.status != 1U) {
            return false;
        }
        static_cast<void>(client.value.receive_queue().mark_not_working());
        if (client.value.send_queue().put({go_first_stream_id, 0U, 1U}) !=
                shmipc::shm::QueueError::none ||
            !client.value.send_queue().mark_working() ||
            !send_polling(sockets.value.client) ||
            !server.value.wait_remote_close(5s)) {
            return false;
        }
        static_cast<void>(server.value.close());
        static_cast<void>(dispatcher.value.stop());
    }
    return ::access(config.queue_path.c_str(), F_OK) != 0 &&
           ::access(config.buffer_path.c_str(), F_OK) != 0;
}

bool test_invalid_state() {
    shmipc::core::V2ServerSession session;
    const std::vector<std::uint8_t> data{1U};
    return session.send(data).error ==
               shmipc::core::V2SessionError::invalid_argument &&
           session.wait_stream(1ms).error ==
               shmipc::core::V2SessionError::invalid_argument;
}

}  // namespace

int main() {
    if (!test_multi_message_round_trip()) {
        std::cerr << "v2 server multi-message round-trip test failed\n";
        return 1;
    }
    if (!test_invalid_state()) {
        std::cerr << "v2 server invalid-state test failed\n";
        return 1;
    }
    return 0;
}
