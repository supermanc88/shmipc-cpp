#include <shmipc/session.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    if (argc != 6) {
        std::cerr << "usage: " << argv[0]
                  << " HOST PORT QUEUE_PATH BUFFER_PATH MESSAGE\n";
        return EXIT_FAILURE;
    }

    const auto parsed_port = std::strtoul(argv[2], nullptr, 10);
    if (parsed_port == 0U || parsed_port > 65535U) {
        std::cerr << "invalid port\n";
        return EXIT_FAILURE;
    }

    shmipc::ClientConfig config;
    config.queue_name = argv[3];
    config.buffer_name = argv[4];

    auto connected = shmipc::connect_tcp(
        argv[1], static_cast<std::uint16_t>(parsed_port), config);
    if (!connected) {
        std::cerr << "connect failed: "
                  << shmipc::to_string(connected.status.error) << '\n';
        return EXIT_FAILURE;
    }

    auto opened = connected.value.open_stream();
    if (!opened) {
        std::cerr << "open stream failed: "
                  << shmipc::to_string(opened.status.error) << '\n';
        return EXIT_FAILURE;
    }

    const std::string message = argv[5];
    const std::vector<std::uint8_t> request(message.begin(), message.end());
    auto status = opened.value.send(request);
    if (!status) {
        std::cerr << "send failed: " << shmipc::to_string(status.error) << '\n';
        return EXIT_FAILURE;
    }

    auto response = opened.value.receive(5s);
    if (!response) {
        std::cerr << "receive failed: "
                  << shmipc::to_string(response.status.error) << '\n';
        return EXIT_FAILURE;
    }
    std::cout.write(reinterpret_cast<const char*>(response.value.data()),
                    static_cast<std::streamsize>(response.value.size()));
    std::cout << '\n';

    status = opened.value.close();
    if (!status) {
        std::cerr << "close stream failed: "
                  << shmipc::to_string(status.error) << '\n';
        return EXIT_FAILURE;
    }
    status = connected.value.close();
    if (!status) {
        std::cerr << "close session failed: "
                  << shmipc::to_string(status.error) << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
