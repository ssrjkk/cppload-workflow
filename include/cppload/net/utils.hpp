// @author ssrjkk | volley
#pragma once

#include <string>

namespace cppload::net {

inline bool host_is_ip_literal(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    if (host.empty()) return false;
    for (char c : host) {
        if (!(c == '.' || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

} // namespace cppload::net
