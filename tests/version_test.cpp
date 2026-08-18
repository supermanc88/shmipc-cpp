#include <shmipc/version.hpp>

#include <cstring>
#include <iostream>

int main() {
    if (shmipc::version.major != 0 || shmipc::version.minor != 1 ||
        shmipc::version.patch != 0) {
        std::cerr << "unexpected structured version\n";
        return 1;
    }

    if (std::strcmp(shmipc::library_version(), "0.1.0") != 0) {
        std::cerr << "unexpected linked library version\n";
        return 1;
    }

    return 0;
}
