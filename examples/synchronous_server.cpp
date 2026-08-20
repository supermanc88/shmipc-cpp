#include <shmipc/listener.hpp>
#include <shmipc/stream_connection.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    using namespace std::chrono_literals;

    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " HOST PORT\n";
        return EXIT_FAILURE;
    }
    const auto parsed_port = std::strtoul(argv[2], nullptr, 10);
    if (parsed_port > 65535U) {
        std::cerr << "invalid port\n";
        return EXIT_FAILURE;
    }

    auto listening = shmipc::listen_tcp(
        argv[1], static_cast<std::uint16_t>(parsed_port));
    if (!listening) {
        std::cerr << "listen failed: "
                  << shmipc::to_string(listening.status.error) << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "listening on port " << listening.value.local_port() << '\n';

    auto accepted = listening.value.accept_session(30s);
    if (!accepted) {
        std::cerr << "accept session failed: "
                  << shmipc::to_string(accepted.status.error) << '\n';
        return EXIT_FAILURE;
    }
    auto stream = accepted.value.accept_stream(30s);
    if (!stream) {
        std::cerr << "accept stream failed: "
                  << shmipc::to_string(stream.status.error) << '\n';
        return EXIT_FAILURE;
    }

    shmipc::StreamConnection connection(std::move(stream.value));
    std::vector<std::uint8_t> request(4096U);
    auto received = connection.read(request.data(), request.size(), 5s);
    if (!received) {
        std::cerr << "read failed: "
                  << shmipc::to_string(received.status.error) << '\n';
        return EXIT_FAILURE;
    }
    const std::string prefix{"echo: "};
    std::vector<std::uint8_t> response(prefix.begin(), prefix.end());
    response.insert(response.end(), request.begin(),
                    request.begin() +
                        static_cast<std::ptrdiff_t>(received.transferred));
    const auto written = connection.write(response.data(), response.size());
    if (!written) {
        std::cerr << "write failed: "
                  << shmipc::to_string(written.status.error) << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
