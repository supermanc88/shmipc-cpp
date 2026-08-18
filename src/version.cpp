#include <shmipc/version.hpp>

namespace shmipc {

const char* library_version() noexcept {
    return version_string.data();
}

}  // namespace shmipc
