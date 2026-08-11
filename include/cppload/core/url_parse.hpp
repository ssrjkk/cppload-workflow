// @author ssrjkk | cppload
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
    if (proto_end != std::string::npos) {
        auto scheme = url.substr(0, proto_end);
        // Recognize secure schemes so wss:// (and https://) keep TLS enabled
        // instead of silently downgrading to a plaintext connection.
        p.tls = (scheme == "https" || scheme == "wss");
    }
    auto default_port = p.tls ? "443" : "80";

    auto path_start = url.find("/", start);
    p.path = (path_start != std::string::npos) ? url.substr(path_start) : "/";
    auto host_port = (path_start != std::string::npos)
        ? url.substr(start, path_start - start)
        : url.substr(start);

    if (host_port.size() > 1 && host_port.front() == '[') {
        auto close = host_port.find(']');
        if (close != std::string::npos) {
            // Host is stored without brackets: resolvers and SNI need the raw
            // literal, while brackets are only re-added when formatting the
            // Host header / URL.
            p.host = host_port.substr(1, close - 1);
            if (close + 1 < host_port.size() && host_port[close + 1] == ':') {
                p.port = host_port.substr(close + 2);
            } else {
                p.port = default_port;
            }
        } else {
            p.host = host_port;
            p.port = default_port;
        }
    } else {
        auto colon = host_port.find(":");
        if (colon != std::string::npos) {
            p.host = host_port.substr(0, colon);
            p.port = host_port.substr(colon + 1);
        } else {
            p.host = host_port;
            p.port = default_port;
        }
    }
    return p;
}

inline std::string sanitize_path(const std::string& path) {
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