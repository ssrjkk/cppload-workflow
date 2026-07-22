#include "cppload/security/tls_context.hpp"
#include <boost/asio/ssl/context.hpp>
#include <stdexcept>

namespace cppload::security {

class TlsContext::Impl {
public:
    explicit Impl(const TlsConfig& config) 
        : ctx_(config.min_tls_version >= 13
            ? boost::asio::ssl::context::tlsv13_client
            : boost::asio::ssl::context::tlsv12_client)
    {
        if (config.verify_peer) {
            ctx_.set_verify_mode(boost::asio::ssl::verify_peer);
            if (!config.ca_cert_file.empty()) {
                ctx_.load_verify_file(config.ca_cert_file);
            } else if (!config.ca_cert_path.empty()) {
                ctx_.add_verify_path(config.ca_cert_path);
            } else {
                // Use default paths
                ctx_.set_default_verify_paths();
            }
        } else {
            ctx_.set_verify_mode(boost::asio::ssl::verify_none);
        }
        
        if (config.use_mtls) {
            if (config.cert_chain_file.empty() || config.private_key_file.empty()) {
                throw std::invalid_argument("mTLS requires cert and key files");
            }
            ctx_.use_certificate_chain_file(config.cert_chain_file);
            ctx_.use_private_key_file(config.private_key_file, boost::asio::ssl::context::pem);
            mtls_enabled_ = true;
        }
        
        if (!config.tmp_dh_file.empty()) {
            ctx_.use_tmp_dh_file(config.tmp_dh_file);
        }
        
        // Set options for security
        ctx_.set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::no_sslv3 |
            boost::asio::ssl::context::no_tlsv1 |
            boost::asio::ssl::context::no_tlsv1_1
        );
    }
    
    boost::asio::ssl::context& get_native_context() {
        return ctx_;
    }
    
    bool is_mtls_enabled() const {
        return mtls_enabled_;
    }
    
private:
    boost::asio::ssl::context ctx_;
    bool mtls_enabled_{false};
};

TlsContext::TlsContext(const TlsConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

TlsContext::~TlsContext() = default;

boost::asio::ssl::context& TlsContext::get_native_context() {
    return impl_->get_native_context();
}

bool TlsContext::is_mtls_enabled() const {
    return impl_->is_mtls_enabled();
}

} // namespace cppload::security
