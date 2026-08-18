#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr std::uint16_t magic = 0x7758;
constexpr std::size_t header_size = 8;
constexpr std::array<const char*, 10> event_names{
    "ShareMemoryByFilePath", "Polling",          "StreamClose",
    "FallbackData",          "ExchangeProtoVersion",
    "ShareMemoryByMemfd",    "AckShareMemory",  "AckReadyRecvFD",
    "HotRestart",            "HotRestartAck",
};

std::string encode_header(std::uint32_t length, std::uint8_t version,
                          std::uint8_t type) {
    const std::array<std::uint8_t, header_size> bytes{
        static_cast<std::uint8_t>((length >> 24U) & 0xffU),
        static_cast<std::uint8_t>((length >> 16U) & 0xffU),
        static_cast<std::uint8_t>((length >> 8U) & 0xffU),
        static_cast<std::uint8_t>(length & 0xffU),
        static_cast<std::uint8_t>((magic >> 8U) & 0xffU),
        static_cast<std::uint8_t>(magic & 0xffU),
        version,
        type,
    };

    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        encoded << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return encoded.str();
}

}  // namespace

int main() {
    std::ifstream golden(SHMIPC_CONTROL_HEADER_GOLDEN_PATH);
    if (!golden) {
        std::cerr << "cannot open control-header golden\n";
        return 1;
    }

    std::size_t row = 0;
    std::string line;
    while (std::getline(golden, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::string name;
        std::uint32_t length = 0;
        unsigned int version = 0;
        unsigned int type = 0;
        std::string expected;
        std::string trailing;
        std::istringstream fields(line);
        if (!(fields >> name >> length >> version >> type >> expected) ||
            (fields >> trailing)) {
            std::cerr << "invalid golden row: " << line << '\n';
            return 1;
        }
        if (row >= event_names.size() || name != event_names[row] || type != row ||
            version > 0xffU || type > 0xffU) {
            std::cerr << "unexpected event metadata: " << line << '\n';
            return 1;
        }

        const auto actual = encode_header(length, static_cast<std::uint8_t>(version),
                                          static_cast<std::uint8_t>(type));
        if (actual != expected) {
            std::cerr << "header mismatch for " << name << ": expected " << expected
                      << ", got " << actual << '\n';
            return 1;
        }
        ++row;
    }

    if (row != event_names.size()) {
        std::cerr << "expected " << event_names.size() << " event rows, got " << row
                  << '\n';
        return 1;
    }
    return 0;
}
