// @author ssrjkk | cppload
#pragma once

#include <string>
#include <memory>
#include <boost/asio/ssl/context.hpp>

namespace cppload::security {

enum class TlsVersion : int {
    TLS_1_2 = 12,
    TLS_1_3 = 13,
};

struct TlsConfig {
    bool verify_peer{true};
    std::string cert_chain_file;
    std::string private_key_file;
    std::string tmp_dh_file;
    std::string ca_cert_file;
    std::string ca_cert_path;
    bool use_mtls{false};
    TlsVersion min_tls_version{TlsVersion::TLS_1_2};
};

class TlsContext {
public:
    explicit TlsContext(const TlsConfig& config = {});
    ~TlsContext();
    
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;
    TlsContext(TlsContext&&) = delete;
    TlsContext& operator=(TlsContext&&) = delete;
    
    [[nodiscard]] boost::asio::ssl::context& get_native_context();
    [[nodiscard]] std::shared_ptr<boost::asio::ssl::context> shared_ctx();
    [[nodiscard]] bool is_mtls_enabled() const;
    [[nodiscard]] bool is_verify_enabled() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::security