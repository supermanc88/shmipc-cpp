#pragma once

#include <string_view>

namespace shmipc {

struct Version final {
    int major;
    int minor;
    int patch;
};

inline constexpr Version version{0, 1, 0};
inline constexpr std::string_view version_string{"0.1.0"};

[[nodiscard]] const char* library_version() noexcept;

}  // namespace shmipc
