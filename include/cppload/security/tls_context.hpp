#pragma once

#include <string>
#include <memory>
#include <boost/asio/ssl/context.hpp>

namespace cppload::security {

struct TlsConfig {
    bool verify_peer{true};
    std::string cert_chain_file;
    std::string private_key_file;
    std::string tmp_dh_file;
    std::string ca_cert_file;
    std::string ca_cert_path;
    bool use_mtls{false};
    int min_tls_version{12};  // 12 = TLSv1.2, 13 = TLSv1.3
};

class TlsContext {
public:
    explicit TlsContext(const TlsConfig& config = {});
    ~TlsContext();
    
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;
    
    [[nodiscard]] boost::asio::ssl::context& get_native_context();
    [[nodiscard]] bool is_mtls_enabled() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::security
