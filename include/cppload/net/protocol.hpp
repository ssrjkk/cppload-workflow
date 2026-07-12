#pragma once

#include "cppload/error.hpp"
#include "cppload/net/connection.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cppload::net {

struct Request {
    std::string method{"GET"};
    std::string path{"/"};
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string host;
    uint16_t port{80};
    bool use_tls{false};
    std::string protocol{"http1.1"};
};

struct Response {
    unsigned int status_code{0};
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::microseconds latency{0};
    std::error_code ec;
};

class ProtocolClient {
public:
    virtual ~ProtocolClient() = default;

    virtual void async_request(
        const Request& req,
        std::function<void(std::error_code, Response)> handler) = 0;

    virtual void set_timeout(std::chrono::milliseconds ms) = 0;

    virtual std::string_view name() const = 0;
};

} // namespace cppload::net
