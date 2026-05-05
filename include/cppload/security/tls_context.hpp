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
};

class TlsContext {
public:
    explicit TlsContext(const TlsConfig& config = {});
    ~TlsContext();
    
    TlsContext(const TlsContext&) = delete;
    TlsContext& operator=(const TlsContext&) = delete;
    
    boost::asio::ssl::context& get_native_context();
    bool is_mtls_enabled() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::security
