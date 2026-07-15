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

class TcpRawClient final : public ProtocolClient {
public:
    TcpRawClient(
        boost::asio::io_context& ioc,
        const security::TlsConfig& tls_config = {});

    ~TcpRawClient() override;

    TcpRawClient(const TcpRawClient&) = delete;
    TcpRawClient& operator=(const TcpRawClient&) = delete;
    TcpRawClient(TcpRawClient&&) noexcept;
    TcpRawClient& operator=(TcpRawClient&&) noexcept;

    void async_request(
        const Request& req,
        std::function<void(std::error_code, Response)> handler) override;

    void set_timeout(std::chrono::milliseconds ms) override;

    std::string_view name() const override { return "tcp_raw"; }

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace cppload::net
