#pragma once

#include "cppload/net/protocol.hpp"
#include "cppload/security/tls_context.hpp"
#include <memory>
#include <string>

namespace cppload::net {

class ProtocolFactory {
public:
    static std::unique_ptr<ProtocolClient> create(
        const std::string& protocol_name,
        boost::asio::io_context& ioc,
        const security::TlsConfig& tls_config = {});

    static void set_tls_config(const security::TlsConfig& tls_config);
    static const security::TlsConfig& tls_config();
    static boost::asio::ssl::context* ssl_context();
    static bool ensure_tls_context();

private:
    static security::TlsConfig global_tls_config_;
    static std::unique_ptr<security::TlsContext> global_tls_ctx_;
};

} // namespace cppload::net
