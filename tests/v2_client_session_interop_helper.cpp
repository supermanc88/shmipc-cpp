#include "core/v2_client_session.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;

bool parse_port(const char* text, std::uint16_t& port) {
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0U || value > UINT16_MAX) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

void print_status(const shmipc::core::V2SessionStatus& status) {
    std::cerr << "session=" << shmipc::core::to_string(status.error)
              << " transport="
              << shmipc::transport::to_string(status.transport_error)
              << " queue=" << shmipc::shm::to_string(status.queue_error)
              << " pool="
              << shmipc::shm::to_string(status.buffer_pool_error)
              << " buffer="
              << shmipc::shm::to_string(status.buffer_io_error)
              << " errno=" << status.system_error << '\n';
}

bool run(const std::string& host, std::uint16_t port,
         const std::string& queue_path, const std::string& buffer_path) {
    auto socket = shmipc::transport::connect_tcp(host, port);
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!socket || !dispatcher) {
        std::cerr << "transport setup failed\n";
        return false;
    }
    const shmipc::core::V2ClientConfig config{
        queue_path, buffer_path, 64U, 1U << 20U,
        {{4096U, 60U}, {8192U, 40U}}};
    auto session = shmipc::core::start_v2_client_session(
        std::move(socket.value), config, dispatcher.value);
    if (!session) {
        print_status(session.status);
        return false;
    }
    const std::vector<std::uint8_t> request(20000U, 0x3cU);
    const std::vector<std::uint8_t> expected(17000U, 0x6bU);
    const auto sent = session.value.send(request);
    if (!sent) {
        print_status(sent);
        return false;
    }
    auto response = session.value.receive(5s);
    if (!response || response.value != expected) {
        print_status(response.status);
        return false;
    }
    const auto closed = session.value.close_stream();
    if (!closed) {
        print_status(closed);
        return false;
    }
    const auto remote_closed = session.value.wait_remote_close(5s);
    if (!remote_closed) {
        print_status(remote_closed);
        return false;
    }
    static_cast<void>(session.value.close());
    static_cast<void>(dispatcher.value.stop());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 0;
    if (argc != 6 || std::string(argv[1]) != "client" ||
        !parse_port(argv[3], port)) {
        std::cerr << "usage: v2_client_session_interop_helper client <host> "
                     "<port> <queue> <buffer>\n";
        return 2;
    }
    return run(argv[2], port, argv[4], argv[5]) ? 0 : 1;
}
