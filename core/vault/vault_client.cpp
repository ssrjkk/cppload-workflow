#include "cppload/vault/vault_client.hpp"
#include "cppload/result.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

namespace cppload::vault {

namespace {

struct UrlParts { std::string host, port, path; };

UrlParts parse_url(const std::string& url) {
    UrlParts p;
    auto proto_end = url.find("://");
    auto start = (proto_end != std::string::npos) ? proto_end + 3 : 0;
    auto path_start = url.find("/", start);
    p.path = (path_start != std::string::npos) ? url.substr(path_start) : "/";
    auto host_port = (path_start != std::string::npos)
        ? url.substr(start, path_start - start)
        : url.substr(start);
    auto colon = host_port.find(":");
    if (colon != std::string::npos) {
        p.host = host_port.substr(0, colon);
        p.port = host_port.substr(colon + 1);
    } else {
        p.host = host_port;
        p.port = (url.find("https:") == 0) ? "443" : "80";
    }
    return p;
}

Result<http::response<http::string_body>, Err> do_get(
    const std::string& host,
    const std::string& port,
    const std::string& target,
    const std::unordered_map<std::string, std::string>& headers,
    int timeout_sec)
{
    beast::error_code ec;
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);

    auto results = resolver.resolve(host, port, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::dns_failure);

    stream.expires_after(std::chrono::seconds(timeout_sec));
    stream.connect(results, ec);
    if (ec == beast::error::timeout)
        return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
    if (ec)
        return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "cppload-pro/1.0");
    req.set(http::field::accept, "application/json");
    for (const auto& [k, v] : headers) req.set(k, v);

    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::write(stream, req, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::read(stream, buffer, res, ec);
    if (ec && ec != http::error::end_of_stream)
        return Result<http::response<http::string_body>, Err>::err(Err::read_error);

    beast::error_code shutdown_ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
    return Result<http::response<http::string_body>, Err>::ok(std::move(res));
}

Result<http::response<http::string_body>, Err> do_post(
    const std::string& host,
    const std::string& port,
    const std::string& target,
    const std::string& body,
    const std::unordered_map<std::string, std::string>& headers,
    int timeout_sec)
{
    beast::error_code ec;
    asio::io_context ioc;
    asio::ip::tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);

    auto results = resolver.resolve(host, port, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::dns_failure);

    stream.expires_after(std::chrono::seconds(timeout_sec));
    stream.connect(results, ec);
    if (ec == beast::error::timeout)
        return Result<http::response<http::string_body>, Err>::err(Err::connection_timeout);
    if (ec)
        return Result<http::response<http::string_body>, Err>::err(Err::connection_refused);

    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "cppload-pro/1.0");
    req.set(http::field::content_type, "application/json");
    req.set(http::field::accept, "application/json");
    for (const auto& [k, v] : headers) req.set(k, v);
    req.body() = body;
    req.prepare_payload();

    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::write(stream, req, ec);
    if (ec) return Result<http::response<http::string_body>, Err>::err(Err::write_error);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::read(stream, buffer, res, ec);
    if (ec && ec != http::error::end_of_stream)
        return Result<http::response<http::string_body>, Err>::err(Err::read_error);

    beast::error_code shutdown_ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
    return Result<http::response<http::string_body>, Err>::ok(std::move(res));
}

std::string sanitise_path(const std::string& path) {
    std::string result;
    result.reserve(path.size());
    for (char c : path) {
        if (c == '/' || c == '-' || c == '_' || c == '.' || c == '~'
            || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9')) {
            result += c;
        }
    }
    return result;
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

    Result<std::string, Err> get_secret(const std::string& path, const std::string& key) {
        if (path.empty()) return Result<std::string, Err>::err(Err::invalid_config);
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + sanitise_path(config_.engine_path)
            + "/data/" + sanitise_path(path);

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_get(url.host, url.port, api_path, hdrs, config_.timeout_seconds);
        if (!res) return Result<std::string, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::string, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
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
        std::string api_path = "/v1/" + sanitise_path(config_.engine_path)
            + "/data/" + sanitise_path(path);

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_get(url.host, url.port, api_path, hdrs, config_.timeout_seconds);
        if (!res) return Result<std::unordered_map<std::string, std::string>, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::unordered_map<std::string, std::string>, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
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
        std::string api_path = "/v1/" + sanitise_path(config_.engine_path)
            + "/data/" + sanitise_path(path);

        json body;
        json data_obj;
        for (const auto& [k, v] : data) data_obj[k] = v;
        body["data"] = data_obj;

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_post(url.host, url.port, api_path,
            body.dump(), hdrs, config_.timeout_seconds);
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
        std::string safe_role = sanitise_path(role_name);
        std::string api_path = "/v1/database/creds/" + safe_role;

        std::unordered_map<std::string, std::string> hdrs;
        hdrs["X-Vault-Token"] = config_.token;

        auto res = do_get(url.host, url.port, api_path, hdrs, config_.timeout_seconds);
        if (!res) return Result<std::string, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::string, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
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

        auto res = do_post(url.host, url.port, api_path,
            body.dump(), hdrs, config_.timeout_seconds);
        if (!res) return Result<std::string, Err>::err(res.error());

        auto status = check_response(res.value());
        if (!status) return Result<std::string, Err>::err(status.error());

        try {
            auto j = json::parse(res.value().body());
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

        asio::io_context ioc;
        asio::ip::tcp::resolver resolver(ioc);
        beast::error_code resolve_ec;
        auto results = resolver.resolve(url.host, url.port, resolve_ec);
        if (resolve_ec) { connected_ = false; return; }

        beast::tcp_stream stream(ioc);
        std::error_code connect_ec;
        bool connect_done = false;
        stream.async_connect(results, [&](std::error_code ec, auto) {
            connect_ec = ec;
            connect_done = true;
        });

        stream.expires_after(std::chrono::seconds(config_.timeout_seconds));
        ioc.run_for(std::chrono::seconds(config_.timeout_seconds));

        if (!connect_done || connect_ec) {
            beast::error_code close_ec;
            stream.socket().close(close_ec);
            connected_ = false;
            return;
        }

        http::request<http::string_body> req{http::verb::get, api_path, 11};
        req.set(http::field::host, url.host);
        req.set(http::field::user_agent, "cppload-pro/1.0");
        req.set(http::field::accept, "application/json");
        for (const auto& [k, v] : hdrs) req.set(k, v);

        stream.expires_after(std::chrono::seconds(config_.timeout_seconds));
        beast::error_code ec;
        http::write(stream, req, ec);
        if (ec) { connected_ = false; return; }

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        stream.expires_after(std::chrono::seconds(config_.timeout_seconds));
        http::read(stream, buffer, res, ec);
        if (ec && ec != http::error::end_of_stream) { connected_ = false; return; }

        beast::error_code shutdown_ec;
        stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);

        connected_ = (res.result_int() >= 200 && res.result_int() < 500);
    }

    VaultConfig config_;
    bool connected_;
};

VaultClient::VaultClient(const VaultConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

VaultClient::~VaultClient() = default;

bool VaultClient::is_connected() const { return impl_->is_connected(); }

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
