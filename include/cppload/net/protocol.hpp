// @author ssrjkk | volley
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
    uint16_t status_code{0};
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::microseconds latency{0};
    std::error_code ec;
    bool body_truncated{false};
};

class ProtocolClient {
protected:
    ProtocolClient() = default;

public:
    ProtocolClient(const ProtocolClient&) = delete;
    ProtocolClient& operator=(const ProtocolClient&) = delete;
    ProtocolClient(ProtocolClient&&) noexcept = default;
    ProtocolClient& operator=(ProtocolClient&&) noexcept = default;
    virtual ~ProtocolClient() noexcept = default;

    virtual void async_request(
        const Request& req,
        std::function<void(std::error_code, const Response&)> handler) = 0;

    virtual void set_timeout(std::chrono::milliseconds ms) = 0;

    virtual void set_max_body_bytes(size_t bytes) { (void)bytes; }

    [[nodiscard]] virtual std::string_view name() const = 0;
};

} // namespace cppload::net