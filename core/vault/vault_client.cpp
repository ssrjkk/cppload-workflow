// @author ssrjkk | cppload
#include "cppload/vault/vault_client.hpp"
#include "cppload/result.hpp"
#include "cppload/core/constants.hpp"
#include "cppload/core/url_parse.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

namespace cppload::vault {

namespace {

using core::parse_url;
using core::sanitize_path;

Result<http::response<http::string_body>, Err> do_request(
    http::verb method,
    const std::string& host,
    const std::string& port,
    const std::string& target,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    uint32_t timeout_sec,
    bool use_tls)
{
    beast::error_code ec;
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);

    auto results = resolver.resolve(host, port, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::dns_failure);

    auto sec = std::chrono::seconds(timeout_sec);

    auto build_request = [&](http::verb m, const std::string& tgt,
                             const std::string& body_data) {
        http::request<http::string_body> req{m, tgt, core::kHttpVersion};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, core::kUserAgent);
        req.set(http::field::accept, "application/json");
        for (const auto& [k, v] : headers) req.set(k, v);
        if (!body_data.empty()) { req.body() = body_data; req.prepare_payload(); }
        return req;
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
        stream.set_verify_callback(asio::ssl::host_name_verification(host));

        beast::get_lowest_layer(stream).expires_after(sec);
        beast::get_lowest_layer(stream).connect(results, ec);
        if (ec == beast::error::timeout)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
        if (ec)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
            return Result<http::response<http::string_body>, Err>::err(Err::tls_handshake_failed);

        stream.next_layer().expires_after(sec);
        stream.handshake(asio::ssl::stream_base::client, ec);
        if (ec)
            return Result<http::response<http::string_body>, Err>::err(Err::tls_handshake_failed);

        auto req = build_request(method, target, body);
        stream.next_layer().expires_after(sec);
        http::write(stream, req, ec);
        if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        stream.next_layer().expires_after(sec);
        http::read(stream, buffer, res, ec);
        if (ec && ec != http::error::end_of_stream)
            return Result<http::response<http::string_body>, Err>::err(Err::read_error);

        stream.shutdown(ec);
        return Result<http::response<http::string_body>, Err>::ok(std::move(res));
    } else {
        beast::tcp_stream stream(ioc);

        stream.expires_after(sec);
        stream.connect(results, ec);
        if (ec == beast::error::timeout)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
        if (ec)
            return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

        auto req = build_request(method, target, body);
        stream.expires_after(sec);
        http::write(stream, req, ec);
        if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        stream.expires_after(sec);
        http::read(stream, buffer, res, ec);
        if (ec && ec != http::error::end_of_stream)
            return Result<http::response<http::string_body>, Err>::err(Err::read_error);

        beast::error_code shutdown_ec;
        stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
        return Result<http::response<http::string_body>, Err>::ok(std::move(res));
    }
}

Result<int, Err> check_response(const http::response<http::string_body>& res) {
    auto code = res.result_int();
    if (code >= 200 && code < 300) return Result<int, Err>::ok(code);
    if (code == 404) return Result<int, Err>::err(Err::vault_not_found);
    if (code == 403) return Result<int, Err>::err(Err::vault_permission_denied);
    if (code == 401) return Result<int, Err>::err(Err::vault_token_invalid);
    if (code >= 500) return Result<int, Err>::err(Err::vault_server_error);
    return Result<int, Err>::err(Err::unknown);
}

} // anonymous namespace

class VaultClient::Impl {
public:
    explicit Impl(const VaultConfig& config)
        : config_(config), connected_(false)
    {
        health_check();
    }

    bool is_connected() const { return connected_; }

    std::string last_error() const { return last_error_; }

    Result<std::string, Err> get_secret(const std::string& path, const std::string& key) {
        if (path.empty()) return Result<std::string, Err>::err(Err::invalid_config);
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + sanitize_path(config_.engine_path)
            + "/data/" + sanitize_path(path);

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_request(http::verb::get, url.host, url.port, api_path,
            "", hdrs, config_.timeout_seconds, url.tls);
        if (!res) return Result<std::string, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::string, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
            if (!j.contains("data") || !j["data"].contains("data"))
                return Result<std::string, Err>::err(Err::parse_error);
            auto data = j["data"]["data"];
            if (data.is_object() && data.contains(key)) {
                return Result<std::string, Err>::ok(data[key].get<std::string>());
            }
            return Result<std::string, Err>::err(Err::vault_not_found);
        } catch (const json::exception&) {
            return Result<std::string, Err>::err(Err::parse_error);
        }
    }

    Result<std::unordered_map<std::string, std::string>, Err>
    get_secret_map(const std::string& path) {
        if (path.empty())
            return Result<std::unordered_map<std::string, std::string>, Err>::err(Err::invalid_config);

        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + sanitize_path(config_.engine_path)
            + "/data/" + sanitize_path(path);

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_request(http::verb::get, url.host, url.port, api_path,
            "", hdrs, config_.timeout_seconds, url.tls);
        if (!res) return Result<std::unordered_map<std::string, std::string>, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::unordered_map<std::string, std::string>, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
            if (!j.contains("data") || !j["data"].contains("data"))
                return Result<std::unordered_map<std::string, std::string>, Err>::err(Err::parse_error);
            auto data = j["data"]["data"];
            std::unordered_map<std::string, std::string> result;
            if (data.is_object()) {
                for (auto it = data.begin(); it != data.end(); ++it) {
                    result[it.key()] = it.value().get<std::string>();
                }
            }
            return Result<std::unordered_map<std::string, std::string>, Err>::ok(std::move(result));
        } catch (const json::exception&) {
            return Result<std::unordered_map<std::string, std::string>, Err>::err(Err::parse_error);
        }
    }

    Result<bool, Err> put_secret(const std::string& path,
                                  const std::unordered_map<std::string, std::string>& data) {
        if (path.empty()) return Result<bool, Err>::err(Err::invalid_config);
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + sanitize_path(config_.engine_path)
            + "/data/" + sanitize_path(path);

        json body;
        json data_obj;
        for (const auto& [k, v] : data) data_obj[k] = v;
        body["data"] = data_obj;

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_request(http::verb::post, url.host, url.port, api_path,
            body.dump(), hdrs, config_.timeout_seconds, url.tls);
        if (!res) return Result<bool, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<bool, Err>::err(status.error());
        return Result<bool, Err>::ok(true);
    }

    Result<std::string, Err> get_kv_secret(const std::string& path, const std::string& key) {
        return get_secret(path, key);
    }

    Result<std::string, Err> get_database_creds(const std::string& role_name) {
        if (role_name.empty()) return Result<std::string, Err>::err(Err::invalid_config);

        auto url = parse_url(config_.address);
        std::string safe_role = sanitize_path(role_name);
        std::string api_path = "/v1/database/creds/" + safe_role;

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_request(http::verb::get, url.host, url.port, api_path,
            "", hdrs, config_.timeout_seconds, url.tls);
        if (!res) return Result<std::string, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::string, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
            if (!j.contains("data"))
                return Result<std::string, Err>::err(Err::parse_error);
            auto d = j["data"];
            std::string username = d.value("username", "");
            std::string password = d.value("password", "");
            return Result<std::string, Err>::ok(username + ":" + password);
        } catch (const json::exception&) {
            return Result<std::string, Err>::err(Err::parse_error);
        }
    }

    Result<std::string, Err> get_approle_token(const std::string& role_id,
                                                const std::string& secret_id) {
        if (role_id.empty() || secret_id.empty())
            return Result<std::string, Err>::err(Err::invalid_config);

        auto url = parse_url(config_.address);
        std::string api_path = "/v1/auth/approle/login";

        json body;
        body["role_id"] = role_id;
        body["secret_id"] = secret_id;

        std::unordered_map<std::string, std::string> hdrs;

        auto res = do_request(http::verb::post, url.host, url.port, api_path,
            body.dump(), hdrs, config_.timeout_seconds, url.tls);
        if (!res) return Result<std::string, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::string, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
            if (!j.contains("auth") || !j["auth"].contains("client_token"))
                return Result<std::string, Err>::err(Err::parse_error);
            return Result<std::string, Err>::ok(j["auth"]["client_token"].get<std::string>());
        } catch (const json::exception&) {
            return Result<std::string, Err>::err(Err::parse_error);
        }
    }

private:
    void health_check() {
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/sys/health";

        std::unordered_map<std::string, std::string> hdrs;
        if (!config_.token.empty()) hdrs["X-Vault-Token"] = config_.token;

        auto res = do_request(http::verb::get, url.host, url.port, api_path,
            "", hdrs, config_.timeout_seconds, url.tls);

        if (res) {
            connected_ = (res.value().result_int() >= 200 && res.value().result_int() < 500);
        } else {
            connected_ = false;
        }
    }

    VaultConfig config_;
    bool connected_;
    mutable std::string last_error_;
};

VaultClient::VaultClient(const VaultConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

VaultClient::~VaultClient() = default;

bool VaultClient::is_connected() const { return impl_->is_connected(); }

std::string VaultClient::last_error() const { return impl_->last_error(); }

Result<std::string, Err> VaultClient::get_secret(const std::string& path, const std::string& key) {
    return impl_->get_secret(path, key);
}

Result<std::unordered_map<std::string, std::string>, Err>
VaultClient::get_secret_map(const std::string& path) {
    return impl_->get_secret_map(path);
}

Result<bool, Err> VaultClient::put_secret(const std::string& path,
    const std::unordered_map<std::string, std::string>& data) {
    return impl_->put_secret(path, data);
}

Result<std::string, Err> VaultClient::get_kv_secret(const std::string& path, const std::string& key) {
    return impl_->get_kv_secret(path, key);
}

Result<std::string, Err> VaultClient::get_database_creds(const std::string& role_name) {
    return impl_->get_database_creds(role_name);
}

Result<std::string, Err> VaultClient::get_approle_token(const std::string& role_id,
    const std::string& secret_id) {
    return impl_->get_approle_token(role_id, secret_id);
}

} // namespace cppload::vault