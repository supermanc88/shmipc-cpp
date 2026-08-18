#include "protocol/control_codec.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string encode_hex(const std::vector<std::uint8_t>& bytes) {
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
            (fields >> trailing) || version > 0xffU || type > 0xffU) {
            std::cerr << "invalid golden row: " << line << '\n';
            return 1;
        }

        const shmipc::protocol::Header header{
            length,
            static_cast<std::uint8_t>(version),
            static_cast<shmipc::protocol::EventType>(type),
        };
        const auto encoded = shmipc::protocol::encode_header(header);
        if (!encoded || encode_hex(encoded.value) != expected ||
            std::string(shmipc::protocol::to_string(header.type)) != name) {
            std::cerr << "header mismatch for " << name << '\n';
            return 1;
        }

        const auto decoded = shmipc::protocol::decode_header(
            encoded.value.data(), encoded.value.size(), length);
        if (!decoded || decoded.value.length != length ||
            decoded.value.version != version ||
            static_cast<unsigned int>(decoded.value.type) != type) {
            std::cerr << "header decode mismatch for " << name << '\n';
            return 1;
        }
        ++row;
    }

    if (row != 10U) {
        std::cerr << "expected 10 event rows, got " << row << '\n';
        return 1;
    }
    return 0;
}
