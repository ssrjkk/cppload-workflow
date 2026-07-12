#include "cppload/net/protocol_factory.hpp"
#include "cppload/net/http_client.hpp"
#include <stdexcept>

namespace cppload::net {

security::TlsConfig ProtocolFactory::global_tls_config_;
std::unique_ptr<security::TlsContext> ProtocolFactory::global_tls_ctx_;

std::unique_ptr<ProtocolClient> ProtocolFactory::create(
    const std::string& protocol_name,
    boost::asio::io_context& ioc,
    const security::TlsConfig& tls_config)
{
    if (protocol_name == "http1.1" || protocol_name.empty()) {
        return std::make_unique<Http11Client>(ioc, tls_config);
    }

    // Protocols not yet implemented - return error via nullptr
    return nullptr;
}

void ProtocolFactory::set_tls_config(const security::TlsConfig& tls_config) {
    global_tls_config_ = tls_config;
    global_tls_ctx_ = std::make_unique<security::TlsContext>(tls_config);
}

const security::TlsConfig& ProtocolFactory::tls_config() {
    return global_tls_config_;
}

boost::asio::ssl::context* ProtocolFactory::ssl_context() {
    if (global_tls_ctx_) {
        return &global_tls_ctx_->get_native_context();
    }
    return nullptr;
}

bool ProtocolFactory::ensure_tls_context() {
    if (!global_tls_ctx_) {
        global_tls_ctx_ = std::make_unique<security::TlsContext>(global_tls_config_);
    }
    return true;
}

} // namespace cppload::net
