// @author ssrjkk | cppload
#include "cppload/net/protocol_factory.hpp"
#include "cppload/net/http_client.hpp"
#include "cppload/net/tcp_raw_client.hpp"
#include "cppload/net/ws_client.hpp"
#include "cppload/error.hpp"
#include <system_error>
#include <vector>
#include <mutex>

namespace cppload::net {

security::TlsConfig ProtocolFactory::global_tls_config_;
std::shared_ptr<security::TlsContext> ProtocolFactory::global_tls_ctx_;
std::mutex ProtocolFactory::global_mutex_;

std::unordered_map<std::string, ProtocolFactory::FactoryFunc>& ProtocolFactory::registry() {
    static std::unordered_map<std::string, FactoryFunc> map = {
        {"http1.1", [](boost::asio::io_context& ioc,
                       const security::TlsConfig& tls) {
            return std::make_unique<Http11Client>(ioc, tls);
        }},
        {"tcp_raw", [](boost::asio::io_context& ioc,
                       const security::TlsConfig& tls) {
            return std::make_unique<TcpRawClient>(ioc, tls);
        }},
        {"ws", [](boost::asio::io_context& ioc,
                  const security::TlsConfig& tls) {
            return std::make_unique<WsClient>(ioc, tls);
        }}
    };
    return map;
}

std::mutex& ProtocolFactory::registry_mutex() {
    static std::mutex mtx;
    return mtx;
}

std::unique_ptr<ProtocolClient> ProtocolFactory::create(
    const std::string& protocol_name,
    boost::asio::io_context& ioc,
    const security::TlsConfig& tls_config)
{
    std::lock_guard<std::mutex> lock(registry_mutex());
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
    std::lock_guard<std::mutex> lock(registry_mutex());
    registry()[name] = std::move(factory);
}

std::vector<std::string> ProtocolFactory::available_protocols() {
    std::lock_guard<std::mutex> lock(registry_mutex());
    std::vector<std::string> result;
    for (const auto& [name, _] : registry()) {
        result.push_back(name);
    }
    return result;
}

void ProtocolFactory::set_tls_config(const security::TlsConfig& tls_config) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    global_tls_config_ = tls_config;
    global_tls_ctx_ = std::make_shared<security::TlsContext>(tls_config);
}

const security::TlsConfig& ProtocolFactory::tls_config() {
    std::lock_guard<std::mutex> lock(global_mutex_);
    return global_tls_config_;
}

std::shared_ptr<boost::asio::ssl::context> ProtocolFactory::ssl_context() {
    std::lock_guard<std::mutex> lock(global_mutex_);
    if (!global_tls_ctx_) {
        global_tls_ctx_ = std::make_shared<security::TlsContext>(global_tls_config_);
    }
    return global_tls_ctx_->shared_ctx();
}

bool ProtocolFactory::ensure_tls_context() {
    std::lock_guard<std::mutex> lock(global_mutex_);
    if (!global_tls_ctx_) {
        global_tls_ctx_ = std::make_shared<security::TlsContext>(global_tls_config_);
    }
    return true;
}

} // namespace cppload::net