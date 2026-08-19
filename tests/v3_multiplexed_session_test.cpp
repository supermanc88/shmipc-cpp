#include "core/v2_multiplexed_session.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
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

bool test_v3_shared_and_sticky_fallback() {
    auto sockets = make_socket_pair();
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!dispatcher) {
        return dispatcher.error ==
               shmipc::transport::TransportError::unsupported;
    }
    if (!sockets) {
        return false;
    }

    const shmipc::core::V3ClientConfig config{
        "v3-multiplexed-queue", "v3-multiplexed-buffer", 16U,
        64U * 1024U, {{4096U, 100U}}};
    std::promise<shmipc::core::V3MultiplexedServerSessionResult> promise;
    auto future = promise.get_future();
    std::thread server_thread([&] {
        promise.set_value(shmipc::core::start_v3_multiplexed_server_session(
            std::move(sockets.value.server), dispatcher.value));
    });
    auto client = shmipc::core::start_v3_multiplexed_client_session(
        std::move(sockets.value.client), config, dispatcher.value);
    auto server = future.get();
    server_thread.join();
    if (!client || !server) {
        return false;
    }

    auto opened = client.value.open_stream();
    if (!opened || opened.value.id() != 2U) {
        return false;
    }
    auto client_stream = std::move(opened.value);
    const std::array<std::vector<std::uint8_t>, 3> messages{
        {std::vector<std::uint8_t>(1024U, 0x31U),
         std::vector<std::uint8_t>(128U * 1024U, 0x52U),
         std::vector<std::uint8_t>(257U, 0x73U)}};
    for (std::size_t index = 0U; index < messages.size(); ++index) {
        if (!client_stream.send(messages[index]) ||
            client_stream.is_fallback() != (index != 0U)) {
            return false;
        }
    }

    auto accepted = server.value.accept_stream(5s);
    if (!accepted || accepted.value.id() != 2U) {
        return false;
    }
    auto server_stream = std::move(accepted.value);
    for (std::size_t index = 0U; index < messages.size(); ++index) {
        const auto received = server_stream.receive(5s);
        if (!received || received.value != messages[index] ||
            server_stream.is_fallback() != (index != 0U)) {
            return false;
        }
    }

    const std::vector<std::uint8_t> acknowledgement{0x7eU};
    if (!server_stream.send(acknowledgement)) {
        return false;
    }
    const auto received_acknowledgement = client_stream.receive(5s);
    if (!received_acknowledgement ||
        received_acknowledgement.value != acknowledgement ||
        !client_stream.is_fallback()) {
        return false;
    }
    if (!client_stream.close() || !server_stream.wait_remote_close(5s) ||
        !server_stream.close()) {
        return false;
    }

    static_cast<void>(client.value.close());
    static_cast<void>(server.value.close());
    static_cast<void>(dispatcher.value.stop());
    return true;
}

bool test_invalid_state() {
    shmipc::core::V3Stream stream;
    shmipc::core::V3MultiplexedClientSession client;
    shmipc::core::V3MultiplexedServerSession server;
    const std::vector<std::uint8_t> data{1U};
    return stream.id() == 0U && !stream.is_fallback() &&
           stream.send(data).error ==
               shmipc::core::V2SessionError::invalid_argument &&
           client.open_stream().status.error ==
               shmipc::core::V2SessionError::invalid_argument &&
           server.accept_stream(1ms).status.error ==
               shmipc::core::V2SessionError::invalid_argument;
}

} // namespace

int main() {
    if (!test_v3_shared_and_sticky_fallback()) {
        std::cerr << "v3 multiplexed shared/fallback test failed\n";
        return 1;
    }
    if (!test_invalid_state()) {
        std::cerr << "v3 multiplexed invalid-state test failed\n";
        return 1;
    }
    return 0;
}
