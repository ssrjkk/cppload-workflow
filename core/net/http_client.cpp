#include "cppload/net/http_client.hpp"
#include "cppload/net/connection.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

namespace cppload::net {

static std::string url_encode_path(const std::string& raw) {
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
            c == '/' || c == '@' || c == '!' || c == '$' || c == '&' ||
            c == '\'' || c == '(' || c == ')' || c == '*' || c == '+' ||
            c == ',' || c == ';' || c == '=' || c == ':' || c == '?' || c == '%') {
            out << c;
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return out.str();
}

class Http11Client::Impl : public std::enable_shared_from_this<Impl> {
public:
    Impl(asio::io_context& ioc, const security::TlsConfig& tls_config)
        : ioc_(ioc), timeout_(5000)
    {
        if (tls_config.verify_peer) {
            tls_ctx_ = std::make_unique<security::TlsContext>(tls_config);
        }
    }

    void async_request(const Request& req,
                       std::function<void(std::error_code, Response)> handler)
    {
        auto start_time = std::chrono::steady_clock::now();
        auto self = shared_from_this();

        bool use_tls = req.use_tls;
        bool has_tls_ctx = tls_ctx_ != nullptr;

        if (use_tls && !has_tls_ctx) {
            Response resp;
            resp.ec = Err::tls_verify_failed;
            handler(resp.ec, resp);
            return;
        }

        auto response = std::make_shared<Response>();
        auto req_msg = std::make_shared<http::request<http::string_body>>();
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto res = std::make_shared<http::response<http::string_body>>();

        // Validate and sanitize HTTP method
        auto verb = http::string_to_verb(req.method);
        if (verb == http::verb::unknown) {
            response->ec = Err::invalid_method;
            handler(response->ec, *response);
            return;
        }
        req_msg->method(verb);

        // Sanitize target: strip CR/LF and URL-encode to prevent injection
        std::string safe_target = req.path;
        safe_target.erase(std::remove_if(safe_target.begin(), safe_target.end(),
            [](char c) { return c == '\r' || c == '\n'; }), safe_target.end());
        req_msg->target(url_encode_path(safe_target));
        req_msg->version(11);
        req_msg->set(http::field::host, req.host);
        req_msg->set(http::field::user_agent, "cppload-pro/1.0");

        if (!req.body.empty()) {
            req_msg->body() = req.body;
            req_msg->prepare_payload();
        }

        for (const auto& [key, value] : req.headers) {
            req_msg->set(key, value);
        }

        // Resolve and connect
        auto resolver = std::make_shared<asio::ip::tcp::resolver>(ioc_);
        resolver->async_resolve(req.host, std::to_string(req.port),
            [self, resolver, req_msg, response, handler, start_time,
             buffer, res, req, use_tls, has_tls_ctx](
                beast::error_code ec, asio::ip::tcp::resolver::results_type results)
            {
                if (ec) {
                    response->ec = (ec == asio::error::host_not_found)
                        ? Err::dns_failure : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                if (use_tls && has_tls_ctx) {
                    self->connect_tls(results, req, req_msg, response,
                                      handler, start_time, buffer, res);
                } else {
                    self->connect_tcp(results, req_msg, response,
                                      handler, start_time, buffer, res);
                }
            });
    }

    void set_timeout(std::chrono::milliseconds timeout) {
        timeout_ = timeout;
    }

    void set_keep_alive(bool keep_alive) {
        keep_alive_ = keep_alive;
    }

private:
    void connect_tcp(
        asio::ip::tcp::resolver::results_type results,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();
        auto stream = std::make_shared<beast::tcp_stream>(ioc_);
        stream->expires_after(timeout_);

        stream->async_connect(results,
            [self, stream, req_msg, response, handler, start_time, buffer, res](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                self->send_and_receive(stream, req_msg, response, handler,
                                       start_time, buffer, res);
            });
    }

    void connect_tls(
        asio::ip::tcp::resolver::results_type results,
        const Request& req,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();
        auto tcp_stream = std::make_shared<beast::tcp_stream>(ioc_);
        tcp_stream->expires_after(timeout_);

        tcp_stream->async_connect(results,
            [self, tcp_stream, req_msg, response, handler, start_time,
             buffer, res, req](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                // Create SSL stream on top of connected TCP stream
                auto ssl_stream = std::make_shared<
                    asio::ssl::stream<beast::tcp_stream>>(
                        std::move(*tcp_stream),
                        self->tls_ctx_->get_native_context());

                // Set SNI hostname
                if (!SSL_set_tlsext_host_name(
                        ssl_stream->native_handle(), req.host.c_str())) {
                    response->ec = boost::asio::error::invalid_argument;
                    handler(response->ec, *response);
                    return;
                }

                beast::get_lowest_layer(*ssl_stream).expires_after(self->timeout_);
                ssl_stream->async_handshake(asio::ssl::stream_base::client,
                    [self, ssl_stream, req_msg, response, handler,
                     start_time, buffer, res](
                        beast::error_code ec)
                    {
                        if (ec) {
                            response->ec = Err::tls_handshake_failed;
                            handler(response->ec, *response);
                            return;
                        }

                        self->send_and_receive_tls(ssl_stream, req_msg,
                            response, handler, start_time, buffer, res);
                    });
            });
    }

    void send_and_receive(
        std::shared_ptr<beast::tcp_stream> stream,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();

        stream->expires_after(timeout_);
        http::async_write(*stream, *req_msg,
            [self, stream, req_msg, response, handler, start_time, buffer, res](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                stream->expires_after(self->timeout_);
                http::async_read(*stream, *buffer, *res,
                    [self, stream, buffer, res, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        auto end_time = std::chrono::steady_clock::now();
                        response->latency =
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                end_time - start_time);

                        if (ec && ec != http::error::end_of_stream) {
                            response->ec = Err::read_error;
                        } else {
                            response->status_code = res->result_int();
                            response->body = res->body();
                            for (const auto& field : *res) {
                                response->headers[
                                    std::string(field.name_string())] =
                                    std::string(field.value());
                            }
                        }

                        handler(response->ec, *response);
                    });
            });
    }

    void send_and_receive_tls(
        std::shared_ptr<asio::ssl::stream<beast::tcp_stream>> ssl_stream,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();

        beast::get_lowest_layer(*ssl_stream).expires_after(timeout_);
        http::async_write(*ssl_stream, *req_msg,
            [self, ssl_stream, req_msg, response, handler, start_time, buffer, res](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                beast::get_lowest_layer(*ssl_stream).expires_after(self->timeout_);
                http::async_read(*ssl_stream, *buffer, *res,
                    [self, ssl_stream, buffer, res, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        auto end_time = std::chrono::steady_clock::now();
                        response->latency =
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                end_time - start_time);

                        if (ec && ec != http::error::end_of_stream) {
                            response->ec = Err::read_error;
                        } else {
                            response->status_code = res->result_int();
                            response->body = res->body();
                            for (const auto& field : *res) {
                                response->headers[
                                    std::string(field.name_string())] =
                                    std::string(field.value());
                            }
                        }

                        handler(response->ec, *response);
                    });
            });
    }

    asio::io_context& ioc_;
    std::chrono::milliseconds timeout_;
    bool keep_alive_{true};
    std::unique_ptr<security::TlsContext> tls_ctx_;
};

Http11Client::Http11Client(
    asio::io_context& ioc,
    const security::TlsConfig& tls_config)
    : impl_(std::make_shared<Impl>(ioc, tls_config))
{
}

Http11Client::~Http11Client() = default;

Http11Client::Http11Client(Http11Client&&) noexcept = default;
Http11Client& Http11Client::operator=(Http11Client&&) noexcept = default;

void Http11Client::async_request(
    const Request& req,
    std::function<void(std::error_code, Response)> handler)
{
    impl_->async_request(req, handler);
}

void Http11Client::set_timeout(std::chrono::milliseconds ms) {
    impl_->set_timeout(ms);
}

void Http11Client::set_keep_alive(bool keep_alive) {
    impl_->set_keep_alive(keep_alive);
}

} // namespace cppload::net
