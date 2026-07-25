// @author ssrjkk | cppload
#include "cppload/security/auth_provider.hpp"
#include "cppload/security/tls_context.hpp"
#include "cppload/result.hpp"
#include "cppload/core/constants.hpp"
#include "cppload/core/url_encode.hpp"
#include "cppload/core/url_parse.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <mutex>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstdint>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

namespace cppload::security {

namespace {

using core::url_encode;

Result<http::response<http::string_body>, Err> do_sync_post(
    const std::string& host,
    const std::string& port,
    const std::string& target,
    const std::string& body,
    const std::string& content_type,
    bool use_tls = false,
    std::chrono::seconds timeout = std::chrono::seconds(core::kDefaultAuthTimeoutSec))
{
    beast::error_code ec;
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);

    auto const results = resolver.resolve(host, port, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::dns_failure);

    http::request<http::string_body> req{http::verb::post, target, core::kHttpVersion};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, core::kUserAgent);
    req.set(http::field::content_type, content_type);
    req.set(http::field::accept, "application/json");
    req.body() = body;
    req.prepare_payload();

    auto send_receive = [&](auto& stream) -> Result<http::response<http::string_body>, Err> {
        beast::get_lowest_layer(stream).expires_after(timeout);
        beast::get_lowest_layer(stream).connect(results, ec);
        if (ec == beast::error::timeout)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
        if (ec)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

        beast::get_lowest_layer(stream).expires_after(timeout);
        http::write(stream, req, ec);
        if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        beast::get_lowest_layer(stream).expires_after(timeout);
        http::read(stream, buffer, res, ec);
        if (ec && ec != http::error::end_of_stream)
            return Result<http::response<http::string_body>, Err>::err(Err::read_error);

        beast::error_code shutdown_ec;
        beast::get_lowest_layer(stream).socket().shutdown(
            asio::ip::tcp::socket::shutdown_both, shutdown_ec);
        return Result<http::response<http::string_body>, Err>::ok(std::move(res));
    };

    if (use_tls) {
        static asio::ssl::context ssl_ctx(asio::ssl::context::tlsv12_client);
        static std::once_flag ssl_flag;
        std::call_once(ssl_flag, [] {
            ssl_ctx.set_default_verify_paths();
            ssl_ctx.set_options(
                asio::ssl::context::default_workarounds |
                asio::ssl::context::no_sslv2 |
                asio::ssl::context::no_sslv3 |
                asio::ssl::context::no_tlsv1 |
                asio::ssl::context::no_tlsv1_1);
            ssl_ctx.set_verify_mode(asio::ssl::verify_peer);
        });
        asio::ssl::stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        stream.set_verify_callback(asio::ssl::rfc2818_verification(host));

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            return Result<http::response<http::string_body>, Err>::err(Err::tls_handshake_failed);

        beast::get_lowest_layer(stream).expires_after(timeout);
        beast::get_lowest_layer(stream).connect(results, ec);
        if (ec == beast::error::timeout)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
        if (ec)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

        stream.next_layer().expires_after(timeout);
        stream.handshake(asio::ssl::stream_base::client, ec);
        if (ec) return Result<http::response<http::string_body>, Err>::err(Err::tls_handshake_failed);

        stream.next_layer().expires_after(timeout);
        http::write(stream, req, ec);
        if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        stream.next_layer().expires_after(timeout);
        http::read(stream, buffer, res, ec);
        if (ec && ec != http::error::end_of_stream)
            return Result<http::response<http::string_body>, Err>::err(Err::read_error);

        stream.shutdown(ec);
        return Result<http::response<http::string_body>, Err>::ok(std::move(res));
    } else {
        beast::tcp_stream stream(ioc);
        return send_receive(stream);
    }
}

using core::parse_url;

} // anonymous namespace

class AuthProvider::Impl {
public:
    explicit Impl(const AuthConfig& config) : config_(config), token_expiry_() {
        if (config_.type == AuthType::OAUTH2 && !config_.token_endpoint.empty()) {
            auto res = fetch_token();
            if (!res) {
                throw std::runtime_error(make_error_code(res.error()).message());
            }
        }
    }

    void apply_headers(std::unordered_map<std::string, std::string>& headers) {
        if (config_.type == AuthType::API_KEY) {
            headers["X-API-Key"] = config_.api_key;
        } else if (config_.type == AuthType::BEARER_TOKEN) {
            headers["Authorization"] = "Bearer " + config_.token;
        } else if (config_.type == AuthType::OAUTH2) {
            auto token = get_token();
            headers["Authorization"] = "Bearer " + token;
        } else if (config_.type == AuthType::MTLS) {
            headers["X-SSL-Cert"] = "mtls";
        }
    }

    std::string get_auth_header() const {
        if (config_.type == AuthType::API_KEY) {
            return "X-API-Key: " + config_.api_key;
        } else if (config_.type == AuthType::BEARER_TOKEN) {
            return "Authorization: Bearer " + config_.token;
        } else if (config_.type == AuthType::OAUTH2) {
            return "Authorization: Bearer " + get_token();
        } else if (config_.type == AuthType::MTLS) {
            return "X-SSL-Cert: mtls";
        }
        return "";
    }

    Result<bool, Err> refresh_token() {
        if (config_.type == AuthType::MTLS) return Result<bool, Err>::ok(true);
        if (config_.type != AuthType::OAUTH2) return Result<bool, Err>::ok(true);
        return fetch_token();
    }

    bool is_expired() const {
        if (config_.type != AuthType::OAUTH2) return false;
        std::lock_guard<std::mutex> lock(mtx_);
        return std::chrono::system_clock::now() >= token_expiry_;
    }

private:
    std::string get_token() const {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (std::chrono::system_clock::now() < token_expiry_) {
                return current_token_;
            }
        }
        auto result = fetch_token();
        std::lock_guard<std::mutex> lock(mtx_);
        if (!result && current_token_.empty()) {
            throw std::runtime_error("OAuth2 token fetch failed: " +
                make_error_code(result.error()).message());
        }
        return current_token_;
    }

    Result<bool, Err> fetch_token() const {
        auto url = parse_url(config_.token_endpoint);

        std::string body = "grant_type=client_credentials"
            "&client_id=" + url_encode(config_.client_id) +
            "&client_secret=" + url_encode(config_.client_secret);

        auto res = do_sync_post(url.host, url.port, url.path, body,
            "application/x-www-form-urlencoded", url.tls);

        if (!res) return Result<bool, Err>::err(res.error());

        auto& http_res = res.value();
        auto code = http_res.result_int();
        if (code < 200 || code >= 300) {
            if (code == 401) return Result<bool, Err>::err(Err::auth_invalid_client);
            if (code == 400) return Result<bool, Err>::err(Err::auth_invalid_grant);
            return Result<bool, Err>::err(Err::auth_server_error);
        }

        try {
            auto j = json::parse(http_res.body());
            {
                std::lock_guard<std::mutex> lock(mtx_);
                current_token_ = j.value("access_token", "");
                auto expires_in = j.value("expires_in", core::kDefaultTokenExpirySec);
                token_expiry_ = std::chrono::system_clock::now() +
                    std::chrono::seconds(std::max(expires_in - core::kExpiryMarginSec, 1));
            }
            return Result<bool, Err>::ok(true);
        } catch (const json::exception&) {
            return Result<bool, Err>::err(Err::auth_parse_error);
        }
    }

    mutable std::mutex mtx_;
    AuthConfig config_;
    mutable std::string current_token_;
    mutable std::chrono::system_clock::time_point token_expiry_;
};

AuthProvider::AuthProvider(const AuthConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

AuthProvider::~AuthProvider() = default;

void AuthProvider::apply_headers(std::unordered_map<std::string, std::string>& headers) {
    impl_->apply_headers(headers);
}

std::string AuthProvider::get_auth_header() const {
    return impl_->get_auth_header();
}

Result<bool, Err> AuthProvider::refresh_token() {
    return impl_->refresh_token();
}

bool AuthProvider::is_expired() const {
    return impl_->is_expired();
}

} // namespace cppload::security