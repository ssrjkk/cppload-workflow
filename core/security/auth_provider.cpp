#include "cppload/security/auth_provider.hpp"
#include "cppload/security/tls_context.hpp"
#include "cppload/result.hpp"
#include "cppload/core/url_parse.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdint>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

namespace cppload::security {

namespace {

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'
            || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase
                    << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c))
                    << std::nouppercase;
        }
    }
    return escaped.str();
}

Result<http::response<http::string_body>, Err> do_sync_post(
    const std::string& host,
    const std::string& port,
    const std::string& target,
    const std::string& body,
    const std::string& content_type,
    std::chrono::seconds timeout = std::chrono::seconds(10))
{
    beast::error_code ec;
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);

    auto const results = resolver.resolve(host, port, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::dns_failure);

    stream.expires_after(timeout);
    stream.connect(results, ec);
    if (ec == beast::error::timeout)
        return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
    if (ec)
        return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "cppload-pro/1.0");
    req.set(http::field::content_type, content_type);
    req.set(http::field::accept, "application/json");
    req.body() = body;
    req.prepare_payload();

    stream.expires_after(timeout);
    http::write(stream, req, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(timeout);
    http::read(stream, buffer, res, ec);
    if (ec && ec != http::error::end_of_stream)
        return Result<http::response<http::string_body>, Err>::err(Err::read_error);

    beast::error_code shutdown_ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
    return Result<http::response<http::string_body>, Err>::ok(std::move(res));
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
        fetch_token();
        std::lock_guard<std::mutex> lock(mtx_);
        return current_token_;
    }

    Result<bool, Err> fetch_token() const {
        auto url = parse_url(config_.token_endpoint);

        std::string body = "grant_type=client_credentials"
            "&client_id=" + url_encode(config_.client_id) +
            "&client_secret=" + url_encode(config_.client_secret);

        auto res = do_sync_post(url.host, url.port, url.path, body,
            "application/x-www-form-urlencoded");

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
                auto expires_in = j.value("expires_in", 3600);
                token_expiry_ = std::chrono::system_clock::now() +
                    std::chrono::seconds(std::max(expires_in - 60, 1));
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
