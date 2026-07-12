#include "cppload/net/protocol_factory.hpp"
#include "cppload/net/http_client.hpp"
#include "cppload/net/tcp_raw_client.hpp"
#include "cppload/net/ws_client.hpp"
#include "cppload/error.hpp"
#include <system_error>
#include <vector>

namespace cppload::net {

security::TlsConfig ProtocolFactory::global_tls_config_;
std::unique_ptr<security::TlsContext> ProtocolFactory::global_tls_ctx_;

std::unordered_map<std::string, ProtocolFactory::FactoryFunc>& ProtocolFactory::registry() {
    static std::unordered_map<std::string, FactoryFunc> reg;
    static bool init = false;
    if (!init) {
        reg["http1.1"] = [](boost::asio::io_context& ioc,
                             const security::TlsConfig& tls) {
            return std::make_unique<Http11Client>(ioc, tls);
        };
        reg["tcp_raw"] = [](boost::asio::io_context& ioc,
                            const security::TlsConfig& tls) {
            return std::make_unique<TcpRawClient>(ioc, tls);
        };
        reg["ws"] = [](boost::asio::io_context& ioc,
                       const security::TlsConfig& tls) {
            return std::make_unique<WsClient>(ioc, tls);
        };
        init = true;
    }
    return reg;
}

std::unique_ptr<ProtocolClient> ProtocolFactory::create(
    const std::string& protocol_name,
    boost::asio::io_context& ioc,
    const security::TlsConfig& tls_config)
{
    auto& reg = registry();
    std::string key = protocol_name.empty() ? "http1.1" : protocol_name;

    auto it = reg.find(key);
    if (it != reg.end()) {
        return it->second(ioc, tls_config);
    }

    throw std::system_error(make_error_code(Err::not_implemented),
        "protocol \"" + protocol_name + "\" is not registered");
}

void ProtocolFactory::register_protocol(
    const std::string& name,
    FactoryFunc factory)
{
    registry()[name] = std::move(factory);
}

std::vector<std::string> ProtocolFactory::available_protocols() {
    std::vector<std::string> result;
    for (const auto& [name, _] : registry()) {
        result.push_back(name);
    }
    return result;
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
