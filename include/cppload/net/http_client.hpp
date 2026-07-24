// @author ssrjkk | cppload
#pragma once

#include "cppload/net/protocol.hpp"
#include "cppload/security/tls_context.hpp"
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace cppload::net {

class Http11Client final : public ProtocolClient {
public:
    Http11Client(
        boost::asio::io_context& ioc,
        const security::TlsConfig& tls_config = {});

    ~Http11Client() override;

    Http11Client(const Http11Client&) = delete;
    Http11Client& operator=(const Http11Client&) = delete;
    Http11Client(Http11Client&&) noexcept;
    Http11Client& operator=(Http11Client&&) noexcept;

    void async_request(
        const Request& req,
        std::function<void(std::error_code, Response)> handler) override;

    void set_timeout(std::chrono::milliseconds ms) override;
    void set_keep_alive(bool keep_alive);

    std::string_view name() const override { return "http1.1"; }

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace cppload::net