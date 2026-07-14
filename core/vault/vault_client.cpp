#include "cppload/vault/vault_client.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <sstream>
#include <stdexcept>

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

http::response<http::string_body> do_get(
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
    if (ec) throw std::runtime_error("Vault DNS resolve failed");

    stream.expires_after(std::chrono::seconds(timeout_sec));
    stream.connect(results, ec);
    if (ec) throw std::runtime_error("Vault connect failed");

    http::request<http::string_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "cppload-pro/1.0");
    req.set(http::field::accept, "application/json");
    for (const auto& [k, v] : headers) req.set(k, v);

    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::write(stream, req, ec);
    if (ec) throw std::runtime_error("Vault write failed");

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::read(stream, buffer, res, ec);
    if (ec && ec != http::error::end_of_stream)
        throw std::runtime_error("Vault read failed");

    beast::error_code shutdown_ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
    return res;
}

http::response<http::string_body> do_post(
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
    if (ec) throw std::runtime_error("Vault DNS resolve failed");

    stream.expires_after(std::chrono::seconds(timeout_sec));
    stream.connect(results, ec);
    if (ec) throw std::runtime_error("Vault connect failed");

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
    if (ec) throw std::runtime_error("Vault write failed");

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(std::chrono::seconds(timeout_sec));
    http::read(stream, buffer, res, ec);
    if (ec && ec != http::error::end_of_stream)
        throw std::runtime_error("Vault read failed");

    beast::error_code shutdown_ec;
    stream.socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
    return res;
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

} // anonymous namespace

class VaultClient::Impl {
public:
    explicit Impl(const VaultConfig& config)
        : config_(config), connected_(false)
    {
        try {
            health_check();
            connected_ = true;
        } catch (const std::exception&) {
            connected_ = false;
        }
    }

    bool is_connected() const { return connected_; }

    std::string get_secret(const std::string& path, const std::string& key) {
        if (path.empty()) { last_error_ = "Vault: path is empty"; return {}; }
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + config_.engine_path + "/data/" + sanitise_path(path);

        std::unordered_map<std::string, std::string> headers;
        headers["X-Vault-Token"] = config_.token;

        http::response<http::string_body> res;
        try {
            res = do_get(url.host, url.port, api_path, headers, config_.timeout_seconds);
        } catch (const std::runtime_error& e) {
            last_error_ = e.what();
            return {};
        }

        if (res.result_int() < 200 || res.result_int() >= 300) {
            last_error_ = "Vault request failed: " + std::to_string(res.result_int());
            return {};
        }

        try {
            auto j = json::parse(res.body());
            // KV v2 wrapper: { data: { data: { key: value }, metadata: {...} } }
            auto data = j["data"]["data"];
            if (data.is_object() && data.contains(key)) {
                return data[key].get<std::string>();
            }
            last_error_ = "Key not found: " + key;
            return {};
        } catch (const json::exception& e) {
            last_error_ = std::string("JSON parse error: ") + e.what();
            return {};
        }
    }

    std::unordered_map<std::string, std::string> get_secret_map(const std::string& path) {
        if (path.empty()) return {};
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + config_.engine_path + "/data/" + sanitise_path(path);

        std::unordered_map<std::string, std::string> headers;
        headers["X-Vault-Token"] = config_.token;

        http::response<http::string_body> res;
        try {
            res = do_get(url.host, url.port, api_path, headers, config_.timeout_seconds);
        } catch (const std::runtime_error& e) {
            last_error_ = e.what();
            return {};
        }

        if (res.result_int() < 200 || res.result_int() >= 300) {
            last_error_ = "Vault request failed: " + std::to_string(res.result_int());
            return {};
        }

        try {
            auto j = json::parse(res.body());
            auto data = j["data"]["data"];
            std::unordered_map<std::string, std::string> result;
            if (data.is_object()) {
                for (auto it = data.begin(); it != data.end(); ++it) {
                    result[it.key()] = it.value().get<std::string>();
                }
            }
            return result;
        } catch (const json::exception& e) {
            last_error_ = std::string("JSON parse error: ") + e.what();
            return {};
        }
    }

    bool put_secret(const std::string& path,
                   const std::unordered_map<std::string, std::string>& data) {
        if (path.empty()) { last_error_ = "Vault: path is empty"; return false; }
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/" + config_.engine_path + "/data/" + sanitise_path(path);

        json body;
        json data_obj;
        for (const auto& [k, v] : data) data_obj[k] = v;
        body["data"] = data_obj;

        std::unordered_map<std::string, std::string> headers;
        headers["X-Vault-Token"] = config_.token;

        http::response<http::string_body> res;
        try {
            res = do_post(url.host, url.port, api_path,
                body.dump(), headers, config_.timeout_seconds);
        } catch (const std::runtime_error& e) {
            last_error_ = e.what();
            return false;
        }

        return res.result_int() >= 200 && res.result_int() < 300;
    }

    std::string get_kv_secret(const std::string& path, const std::string& key) {
        return get_secret(path, key);
    }

    std::string get_database_creds(const std::string& role_name) {
        if (role_name.empty()) { last_error_ = "Vault: role_name is empty"; return {}; }
        auto url = parse_url(config_.address);
        std::string safe_role = sanitise_path(role_name);
        std::string api_path = "/v1/database/creds/" + safe_role;

        std::unordered_map<std::string, std::string> headers;
        headers["X-Vault-Token"] = config_.token;

        http::response<http::string_body> res;
        try {
            res = do_get(url.host, url.port, api_path, headers, config_.timeout_seconds);
        } catch (const std::runtime_error& e) {
            last_error_ = e.what();
            return {};
        }

        if (res.result_int() < 200 || res.result_int() >= 300) {
            last_error_ = "Vault DB creds failed: " + std::to_string(res.result_int());
            return {};
        }

        try {
            auto j = json::parse(res.body());
            auto data = j["data"];
            std::string username = data.value("username", "");
            std::string password = data.value("password", "");
            return username + ":" + password;
        } catch (const json::exception& e) {
            last_error_ = std::string("JSON parse error: ") + e.what();
            return {};
        }
    }

    std::string get_approle_token(const std::string& role_id,
                                 const std::string& secret_id) {
        if (role_id.empty() || secret_id.empty()) {
            last_error_ = "Vault: role_id and secret_id required";
            return {};
        }
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/auth/approle/login";

        json body;
        body["role_id"] = role_id;
        body["secret_id"] = secret_id;

        std::unordered_map<std::string, std::string> headers;

        http::response<http::string_body> res;
        try {
            res = do_post(url.host, url.port, api_path,
                body.dump(), headers, config_.timeout_seconds);
        } catch (const std::runtime_error& e) {
            last_error_ = e.what();
            return {};
        }

        if (res.result_int() < 200 || res.result_int() >= 300) {
            last_error_ = "Vault AppRole login failed: " + std::to_string(res.result_int());
            return {};
        }

        try {
            auto j = json::parse(res.body());
            return j["auth"]["client_token"].get<std::string>();
        } catch (const json::exception& e) {
            last_error_ = std::string("JSON parse error: ") + e.what();
            return {};
        }
    }

    std::string last_error() const { return last_error_; }

private:
    void health_check() {
        auto url = parse_url(config_.address);
        std::string api_path = "/v1/sys/health";

        std::unordered_map<std::string, std::string> headers;
        if (!config_.token.empty()) headers["X-Vault-Token"] = config_.token;

        try {
            auto res = do_get(url.host, url.port, api_path, headers, config_.timeout_seconds);
            connected_ = (res.result_int() >= 200 && res.result_int() < 500);
        } catch (const std::exception&) {
            connected_ = false;
        }
    }

    VaultConfig config_;
    bool connected_;
    std::string last_error_;
};

VaultClient::VaultClient(const VaultConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

VaultClient::~VaultClient() = default;

bool VaultClient::is_connected() const { return impl_->is_connected(); }
std::string VaultClient::get_secret(const std::string& path, const std::string& key) {
    return impl_->get_secret(path, key);
}
std::unordered_map<std::string, std::string> VaultClient::get_secret_map(const std::string& path) {
    return impl_->get_secret_map(path);
}
bool VaultClient::put_secret(const std::string& path,
    const std::unordered_map<std::string, std::string>& data) {
    return impl_->put_secret(path, data);
}
std::string VaultClient::get_kv_secret(const std::string& path, const std::string& key) {
    return impl_->get_kv_secret(path, key);
}
std::string VaultClient::get_database_creds(const std::string& role_name) {
    return impl_->get_database_creds(role_name);
}
std::string VaultClient::get_approle_token(const std::string& role_id,
    const std::string& secret_id) {
    return impl_->get_approle_token(role_id, secret_id);
}
std::string VaultClient::last_error() const { return impl_->last_error(); }

} // namespace cppload::vault
