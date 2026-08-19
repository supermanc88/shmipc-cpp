#include "core/v2_multiplexed_session.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

void print_status(const shmipc::core::V2SessionStatus& status,
                  const shmipc::core::V3HandshakeStatus& handshake_status) {
    std::cerr << "session=" << shmipc::core::to_string(status.error)
              << " transport="
              << shmipc::transport::to_string(status.transport_error)
              << " queue=" << shmipc::shm::to_string(status.queue_error)
              << " pool=" << shmipc::shm::to_string(status.buffer_pool_error)
              << " buffer="
              << shmipc::shm::to_string(status.buffer_io_error)
              << " handshake="
              << shmipc::core::to_string(handshake_status.error)
              << " errno=" << status.system_error << '\n';
}

std::vector<std::vector<std::uint8_t>> messages() {
    return {std::vector<std::uint8_t>(1024U, 0x31U),
            std::vector<std::uint8_t>(2U * 1024U * 1024U, 0x52U),
            std::vector<std::uint8_t>(257U, 0x73U)};
}

bool run_server(const std::string& socket_path) {
    auto socket = shmipc::transport::connect_unix(socket_path);
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!socket || !dispatcher) {
        return false;
    }
    auto session = shmipc::core::start_v3_multiplexed_server_session(
        std::move(socket.value), dispatcher.value);
    if (!session) {
        print_status(session.status, session.handshake_status);
        return false;
    }
    auto stream = session.value.accept_stream(5s);
    if (!stream) {
        print_status(stream.status, {});
        return false;
    }
    const auto expected = messages();
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto received = stream.value.receive(5s);
        if (!received || received.value != expected[index] ||
            stream.value.is_fallback() != (index != 0U)) {
            print_status(received.status, {});
            return false;
        }
    }
    if (session.value.is_healthy()) {
        return false;
    }
    if (!stream.value.send(std::vector<std::uint8_t>{0x7eU}) ||
        !stream.value.wait_remote_close(5s) || !stream.value.close()) {
        return false;
    }
    static_cast<void>(session.value.close());
    static_cast<void>(dispatcher.value.stop());
    return true;
}

bool run_client(const std::string& socket_path, const std::string& queue_name,
                const std::string& buffer_name) {
    auto socket = shmipc::transport::connect_unix(socket_path);
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!socket || !dispatcher) {
        return false;
    }
    const shmipc::core::V3ClientConfig config{
        queue_name, buffer_name, 16U, 1U << 20U, {{4096U, 100U}}};
    auto session = shmipc::core::start_v3_multiplexed_client_session(
        std::move(socket.value), config, dispatcher.value);
    if (!session) {
        print_status(session.status, session.handshake_status);
        return false;
    }
    auto stream = session.value.open_stream();
    if (!stream) {
        print_status(stream.status, {});
        return false;
    }
    const auto payloads = messages();
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        if (!stream.value.send(payloads[index]) ||
            stream.value.is_fallback() != (index != 0U)) {
            return false;
        }
    }
    if (session.value.is_healthy() ||
        session.value.open_stream().status.error !=
            shmipc::core::V2SessionError::unhealthy) {
        return false;
    }
    const auto acknowledgement = stream.value.receive(5s);
    if (!acknowledgement ||
        acknowledgement.value != std::vector<std::uint8_t>{0x7eU} ||
        !stream.value.is_fallback() || !stream.value.wait_remote_close(5s) ||
        !stream.value.close()) {
        print_status(acknowledgement.status, {});
        return false;
    }
    static_cast<void>(session.value.close());
    static_cast<void>(dispatcher.value.stop());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::string role = argc > 1 ? argv[1] : "";
    if ((role == "server" && argc == 3)) {
        return run_server(argv[2]) ? 0 : 1;
    }
    if (role == "client" && argc == 5) {
        return run_client(argv[2], argv[3], argv[4]) ? 0 : 1;
    }
    std::cerr << "usage: v3_multiplexed_session_interop_helper server "
                 "<unix-socket>\n"
                 "   or: v3_multiplexed_session_interop_helper client "
                 "<unix-socket> <queue-name> <buffer-name>\n";
    return 2;
}
