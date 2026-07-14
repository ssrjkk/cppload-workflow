#pragma once

#include "cppload/net/protocol.hpp"
#include "cppload/security/tls_context.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cppload::net {

class ProtocolFactory {
public:
    using FactoryFunc = std::function<std::unique_ptr<ProtocolClient>(
        boost::asio::io_context&, const security::TlsConfig&)>;

    static std::unique_ptr<ProtocolClient> create(
        const std::string& protocol_name,
        boost::asio::io_context& ioc,
        const security::TlsConfig& tls_config = {});

    static void register_protocol(
        const std::string& name,
        FactoryFunc factory);

    static void set_tls_config(const security::TlsConfig& tls_config);
    static const security::TlsConfig& tls_config();
    static boost::asio::ssl::context* ssl_context();
    static bool ensure_tls_context();

    static std::vector<std::string> available_protocols();

private:
    static security::TlsConfig global_tls_config_;
    static std::unique_ptr<security::TlsContext> global_tls_ctx_;
    static std::mutex global_mutex_;
    static std::unordered_map<std::string, FactoryFunc>& registry();
    static std::mutex& registry_mutex();
};

} // namespace cppload::net
