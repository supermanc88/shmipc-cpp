#include "core/v2_handshake.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

constexpr std::uint32_t queue_capacity = 64U;
constexpr std::size_t buffer_size = 1U << 20U;

bool parse_port(const char* text, std::uint16_t& port) {
    char* end = nullptr;
    const auto value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0' || value == 0U || value > UINT16_MAX) {
        return false;
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

bool validate_resources(shmipc::core::V2SharedMemory& resources) {
    return resources && resources.send_queue().capacity() == queue_capacity &&
           resources.receive_queue().capacity() == queue_capacity &&
           resources.buffer_pool().list_count() == 2U &&
           resources.buffer_pool().min_slice_capacity() == 4096U &&
           resources.buffer_pool().max_slice_capacity() == 8192U;
}

void print_error(const shmipc::core::V2HandshakeResult& result) {
    std::cerr << "handshake=" << shmipc::core::to_string(result.status.error)
              << " transport="
              << shmipc::transport::to_string(result.status.transport_error)
              << " codec="
              << shmipc::protocol::to_string(result.status.codec_error)
              << " mapping="
              << shmipc::shm::to_string(result.status.mapping_error)
              << " pool="
              << shmipc::shm::to_string(result.status.buffer_pool_error)
              << " queue="
              << shmipc::shm::to_string(result.status.queue_error)
              << " errno=" << result.status.system_error << '\n';
}

bool wait_for_signal(const std::string& path) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (::access(path.c_str(), F_OK) == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cerr << "timed out waiting for Go mapping signal\n";
    return false;
}

bool run_server(const std::string& host, std::uint16_t port,
                const std::string& mapped_signal) {
    auto socket = shmipc::transport::connect_tcp(host, port);
    auto result = socket ? shmipc::core::v2_server_handshake(socket.value)
                         : shmipc::core::V2HandshakeResult{};
    if (!socket || !result || result.value.is_creator() ||
        !validate_resources(result.value)) {
        print_error(result);
        return false;
    }
    return wait_for_signal(mapped_signal);
}

bool run_client(const std::string& host, std::uint16_t port,
                const std::string& queue_path,
                const std::string& buffer_path,
                const std::string& mapped_signal) {
    auto socket = shmipc::transport::connect_tcp(host, port);
    const shmipc::core::V2ClientConfig config{
        queue_path, buffer_path, queue_capacity, buffer_size,
        {{4096U, 60U}, {8192U, 40U}}};
    auto result = socket ? shmipc::core::v2_client_handshake(socket.value, config)
                         : shmipc::core::V2HandshakeResult{};
    if (!socket || !result || !result.value.is_creator() ||
        !validate_resources(result.value)) {
        print_error(result);
        return false;
    }
    return wait_for_signal(mapped_signal);
}

}  // namespace

int main(int argc, char** argv) {
    std::uint16_t port = 0;
    if (argc == 5 && parse_port(argv[3], port) &&
        std::string(argv[1]) == "server") {
        return run_server(argv[2], port, argv[4]) ? 0 : 1;
    }
    if (argc == 7 && parse_port(argv[3], port) &&
        std::string(argv[1]) == "client") {
        return run_client(argv[2], port, argv[4], argv[5], argv[6]) ? 0 : 1;
    }
    std::cerr << "usage: v2_handshake_interop_helper server <host> <port> "
                 "<mapped-signal> | "
                 "client <host> <port> <queue> <buffer> <mapped-signal>\n";
    return 2;
}
