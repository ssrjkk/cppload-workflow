#pragma once

#include "cppload/net/protocol.hpp"
#include "cppload/security/tls_context.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>

namespace cppload::net {

class WsClient final : public ProtocolClient {
public:
    WsClient(
        boost::asio::io_context& ioc,
        const security::TlsConfig& tls_config = {});

    ~WsClient() override;

    WsClient(const WsClient&) = delete;
    WsClient& operator=(const WsClient&) = delete;
    WsClient(WsClient&&) noexcept;
    WsClient& operator=(WsClient&&) noexcept;

    void async_request(
        const Request& req,
        std::function<void(std::error_code, Response)> handler) override;

    void set_timeout(std::chrono::milliseconds ms) override;

    std::string_view name() const override { return "ws"; }

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace cppload::net
