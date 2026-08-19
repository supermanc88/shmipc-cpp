#include "core/v3_handshake.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

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

bool validate(shmipc::core::V3SharedMemory& resources, bool creator,
              const std::string& queue_name, const std::string& buffer_name) {
    return resources && resources.is_creator() == creator &&
           resources.queue_name() == queue_name &&
           resources.buffer_name() == buffer_name &&
           resources.queue_fd() >= 0 && resources.buffer_fd() >= 0 &&
           resources.send_queue().capacity() == 64U &&
           resources.receive_queue().capacity() == 64U &&
           resources.buffer_pool().list_count() == 2U &&
           resources.buffer_pool().min_slice_capacity() == 4096U &&
           resources.buffer_pool().max_slice_capacity() == 8192U;
}

void print_status(const shmipc::core::V3HandshakeStatus& status) {
    std::cerr << "handshake=" << shmipc::core::to_string(status.error)
              << " negotiation="
              << shmipc::core::to_string(status.negotiation_status.error)
              << " transport="
              << shmipc::transport::to_string(status.transport_error)
              << " codec=" << shmipc::protocol::to_string(status.codec_error)
              << " mapping=" << shmipc::shm::to_string(status.mapping_error)
              << " pool=" << shmipc::shm::to_string(status.buffer_pool_error)
              << " queue=" << shmipc::shm::to_string(status.queue_error)
              << " errno=" << status.system_error << '\n';
}

bool run_server(const std::string& socket_path, const std::string& queue_name,
                const std::string& buffer_name,
                const std::string& mapped_signal) {
    auto socket = shmipc::transport::connect_unix(socket_path);
    auto result = socket ? shmipc::core::v3_server_handshake(socket.value)
                         : shmipc::core::V3HandshakeResult{};
    if (!socket || !result ||
        !validate(result.value, false, queue_name, buffer_name)) {
        print_status(result.status);
        return false;
    }
    return wait_for_signal(mapped_signal);
}

bool run_client(const std::string& socket_path, const std::string& queue_name,
                const std::string& buffer_name,
                const std::string& mapped_signal) {
    auto socket = shmipc::transport::connect_unix(socket_path);
    const shmipc::core::V3ClientConfig config{
        queue_name, buffer_name, 64U, 1U << 20U, {{4096U, 60U}, {8192U, 40U}}};
    auto result = socket
                      ? shmipc::core::v3_client_handshake(socket.value, config)
                      : shmipc::core::V3HandshakeResult{};
    if (!socket || !result ||
        !validate(result.value, true, queue_name, buffer_name)) {
        print_status(result.status);
        return false;
    }
    return wait_for_signal(mapped_signal);
}

} // namespace

int main(int argc, char** argv) {
    const std::string role = argc > 1 ? argv[1] : "";
    if (argc != 6 || (role != "client" && role != "server")) {
        std::cerr
            << "usage: v3_handshake_interop_helper <client|server> "
               "<unix-socket> <queue-name> <buffer-name> <mapped-signal>\n";
        return 2;
    }
    const bool success = role == "client"
                             ? run_client(argv[2], argv[3], argv[4], argv[5])
                             : run_server(argv[2], argv[3], argv[4], argv[5]);
    return success ? 0 : 1;
}
