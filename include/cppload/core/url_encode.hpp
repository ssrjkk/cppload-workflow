// @author ssrjkk | cppload
#pragma once

#include <array>
#include <cctype>
#include <string>

namespace cppload::core {

inline std::string url_encode(const std::string& value) {
    static constexpr std::array<char, 17> hex{'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','\0'};
    std::string result;
    result.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%';
            result += hex[c >> 4]; // NOLINT cppcoreguidelines-pro-bounds-constant-array-index
            result += hex[c & 0x0F]; // NOLINT cppcoreguidelines-pro-bounds-constant-array-index
        }
    }
    return result;
}

} // namespace cppload::core