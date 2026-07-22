#pragma once

#include <string>

namespace cppload::core {

struct UrlParts {
    std::string host;
    std::string port;
    std::string path;
    bool tls{false};
};

inline UrlParts parse_url(const std::string& url) {
    UrlParts p;
    auto proto_end = url.find("://");
    auto start = (proto_end != std::string::npos) ? proto_end + 3 : 0;
    auto path_start = url.find("/", start);
    p.path = (path_start != std::string::npos) ? url.substr(path_start) : "/";
    auto host_port = (path_start != std::string::npos)
        ? url.substr(start, path_start - start)
        : url.substr(start);
    auto colon = host_port.find(":");
    if (colon != std::string::npos) {
        p.host = host_port.substr(0, colon);
        p.port = host_port.substr(colon + 1);
    } else {
        p.host = host_port;
        p.port = (proto_end != std::string::npos &&
                  url.substr(0, proto_end) == "https") ? "443" : "80";
    }
    p.tls = (proto_end != std::string::npos &&
             url.substr(0, proto_end) == "https");
    return p;
}

inline std::string sanitise_path(const std::string& path) {
    std::string result;
    result.reserve(path.size());
    for (char c : path) {
        if (c == '/' || c == '-' || c == '_' || c == '.' || c == '~'
            || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')) {
            result += c;
        }
    }
    return result;
}

} // namespace cppload::core
