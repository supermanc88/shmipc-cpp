#include "core/v2_server_session.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

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
    std::cerr
        << "session=" << shmipc::core::to_string(status.error) << " handshake="
        << shmipc::core::to_string(status.handshake_status.error) << " mapping="
        << shmipc::shm::to_string(status.handshake_status.mapping_error)
        << " handshake_pool="
        << shmipc::shm::to_string(status.handshake_status.buffer_pool_error)
        << " handshake_queue="
        << shmipc::shm::to_string(status.handshake_status.queue_error)
        << " transport=" << shmipc::transport::to_string(status.transport_error)
        << " queue=" << shmipc::shm::to_string(status.queue_error)
        << " pool=" << shmipc::shm::to_string(status.buffer_pool_error)
        << " buffer=" << shmipc::shm::to_string(status.buffer_io_error)
        << " errno=" << status.system_error << '\n';
}

bool wait_for_file(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (::access(path.c_str(), F_OK) == 0) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

bool run(const std::string& host, std::uint16_t port,
         const std::string& scenario, const std::string& signal_path) {
    auto socket = shmipc::transport::connect_tcp(host, port);
    auto dispatcher = shmipc::transport::start_epoll_dispatcher();
    if (!socket || !dispatcher) {
        std::cerr << "transport setup failed\n";
        return false;
    }
    auto session = shmipc::core::start_v2_server_session(
        std::move(socket.value), dispatcher.value);
    if (!session) {
        print_status(session.status);
        return false;
    }
    const std::vector<std::vector<std::uint8_t>> expected{
        std::vector<std::uint8_t>(31U, 0x11U),
        std::vector<std::uint8_t>(20000U, 0x22U),
        std::vector<std::uint8_t>(7000U, 0x33U)};
    if (!session.value.wait_stream(5s) || session.value.stream_id() != 2U) {
        std::cerr << "first Go stream was not accepted as ID 2\n";
        return false;
    }
    const auto request_count =
        scenario == "remote-close" ? 1U : expected.size();
    for (std::size_t index = 0; index < request_count; ++index) {
        const auto& payload = expected[index];
        auto message = session.value.receive(5s);
        if (!message || message.value != payload) {
            print_status(message.status);
            return false;
        }
    }
    if (scenario == "remote-close") {
        const auto remote_close = session.value.wait_remote_close(5s);
        if (!remote_close) {
            print_status(remote_close);
            return false;
        }
        static_cast<void>(session.value.close());
        static_cast<void>(dispatcher.value.stop());
        return true;
    }
    if (scenario != "roundtrip") {
        std::cerr << "unknown scenario\n";
        return false;
    }
    const std::vector<std::vector<std::uint8_t>> responses{
        std::vector<std::uint8_t>(47U, 0x44U),
        std::vector<std::uint8_t>(17000U, 0x55U),
        std::vector<std::uint8_t>(9000U, 0x66U)};
    for (const auto& payload : responses) {
        const auto sent = session.value.send(payload);
        if (!sent) {
            print_status(sent);
            return false;
        }
    }
    const auto local_close = session.value.close_stream();
    if (!local_close) {
        print_status(local_close);
        return false;
    }
    if (!wait_for_file(signal_path)) {
        std::cerr << "Go client did not observe the remote close\n";
        return false;
    }
    static_cast<void>(session.value.close());
    static_cast<void>(dispatcher.value.stop());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 0;
    if (argc != 6 || std::string(argv[1]) != "server" ||
        !parse_port(argv[3], port)) {
        std::cerr
            << "usage: v2_server_session_interop_helper server <host> <port> "
               "<scenario> <signal>\n";
        return 2;
    }
    return run(argv[2], port, argv[4], argv[5]) ? 0 : 1;
}
