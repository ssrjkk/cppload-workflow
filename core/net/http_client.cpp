// @author ssrjkk | cppload
#include "cppload/net/http_client.hpp"
#include "cppload/net/connection.hpp"
#include "cppload/core/constants.hpp"
#include "cppload/core/url_encode.hpp"
#include <boost/beast/http.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

namespace cppload::net {

static std::string url_encode_path(const std::string& raw) {
    static constexpr char hex_chars[] = "0123456789ABCDEF";
    static constexpr std::string_view unreserved =
        "/@!$&'()*+,;=:?._-";
    std::string out;
    out.reserve(raw.size() * 3);
    for (unsigned char c : raw) {
        if (std::isalnum(c) || unreserved.find(static_cast<char>(c)) != std::string_view::npos) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex_chars[c >> 4];
            out += hex_chars[c & 0x0F];
        }
    }
    return out;
}

// True for both IPv4 dotted-quad literals and IPv6 literals (contain ':').
static bool host_is_ip_literal(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    if (host.empty()) return false;
    for (char c : host) {
        if (!(c == '.' || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

// RFC 7230 §6.3.2: a request sent on a reused connection that turns out to be
// closed may be retried on a fresh connection, but only for safe/idempotent
// methods (a blind retry of POST/PATCH could duplicate a side effect).
static bool method_is_idempotent(const std::string& method) {
    return method == "GET" || method == "HEAD" || method == "PUT" ||
           method == "DELETE" || method == "OPTIONS" || method == "TRACE";
}

// Host header value per RFC 9110: IPv6 literals must be bracket-quoted, and
// the default port for the scheme is omitted.
static std::string format_host_header(const std::string& host,
                                      uint16_t port,
                                      bool use_tls) {
    std::string h = (host.find(':') != std::string::npos)
        ? "[" + host + "]" : host;
    uint16_t default_port = use_tls ? 443 : 80;
    if (port != default_port) {
        h += ':';
        h += std::to_string(port);
    }
    return h;
}

class Http11Client::Impl : public std::enable_shared_from_this<Impl> {
public:
    Impl(asio::io_context& ioc, const security::TlsConfig& tls_config,
         bool keep_alive)
        : ioc_(ioc), keep_alive_(keep_alive)
    {
        tls_ctx_ = std::make_unique<security::TlsContext>(tls_config);
    }

    void async_request(const Request& req,
                       std::function<void(std::error_code, Response)> handler)
    {
        auto start_time = std::chrono::steady_clock::now();
        auto self = shared_from_this();

        bool use_tls = req.use_tls;
        if (use_tls && tls_ctx_ == nullptr) {
            Response resp;
            resp.ec = Err::tls_verify_failed;
            handler(resp.ec, resp);
            return;
        }

        // Keep-alive reuse: if the cached connection still targets the same
        // endpoint it is written to and read from directly. A stale cached
        // connection falls back to a fresh connect inside the cached path.
        auto cached = cached_;
        if (keep_alive_.load(std::memory_order_relaxed) && cached &&
            cached->host == req.host && cached->port == req.port &&
            cached->tls == use_tls) {
            request_on_cached(cached, req, handler, start_time);
            return;
        }

        request_fresh(req, handler, start_time);
    }

    void set_timeout(std::chrono::milliseconds timeout) {
        timeout_ms_.store(timeout.count(), std::memory_order_relaxed);
    }

    void set_keep_alive(bool keep_alive) {
        keep_alive_.store(keep_alive, std::memory_order_relaxed);
        if (!keep_alive) cached_.reset();
    }

private:
    // State of a persistent connection eligible for keep-alive reuse. For TLS
    // connections only ssl is set (the TCP stream lives inside it); for
    // plaintext only tcp is set. The buffer is shared across requests on the
    // same connection so Beast can carry any leftover bytes between messages.
    struct ReusableConn {
        std::shared_ptr<beast::tcp_stream> tcp;
        std::shared_ptr<asio::ssl::stream<beast::tcp_stream>> ssl;
        std::shared_ptr<beast::flat_buffer> buffer;
        std::string host;
        uint16_t port{0};
        bool tls{false};
    };

    std::shared_ptr<http::request<http::string_body>>
    build_request(const Request& req, std::shared_ptr<Response> response) {
        auto req_msg = std::make_shared<http::request<http::string_body>>();
        auto verb = http::string_to_verb(req.method);
        if (verb == http::verb::unknown) {
            response->ec = Err::invalid_method;
            return nullptr;
        }
        req_msg->method(verb);

        // Sanitize target: strip CR/LF and URL-encode to prevent injection
        std::string safe_target = req.path;
        safe_target.erase(std::remove_if(safe_target.begin(), safe_target.end(),
            [](char c) { return c == '\r' || c == '\n'; }), safe_target.end());
        req_msg->target(url_encode_path(safe_target));
        req_msg->version(core::kHttpVersion);
        req_msg->set(http::field::host, format_host_header(req.host, req.port, req.use_tls));
        req_msg->set(http::field::user_agent, core::kUserAgent);

        if (!req.body.empty()) {
            req_msg->body() = req.body;
            req_msg->prepare_payload();
        }

        for (const auto& [key, value] : req.headers) {
            req_msg->set(key, value);
        }
        return req_msg;
    }

    void request_fresh(
        const Request& req,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time)
    {
        auto self = shared_from_this();
        auto response = std::make_shared<Response>();
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto res = std::make_shared<http::response<http::string_body>>();
        auto req_msg = build_request(req, response);
        if (!req_msg) {
            handler(response->ec, *response);
            return;
        }

        bool use_tls = req.use_tls;
        auto resolver = std::make_shared<asio::ip::tcp::resolver>(ioc_);
        resolver->async_resolve(req.host, std::to_string(req.port),
            [self, resolver, req_msg, response, handler, start_time,
             buffer, res, req, use_tls](
                beast::error_code ec, asio::ip::tcp::resolver::results_type results)
            {
                if (ec) {
                    response->ec = (ec == asio::error::host_not_found)
                        ? Err::dns_failure : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                if (use_tls) {
                    self->connect_tls(results, req, req_msg, response,
                                      handler, start_time, buffer, res);
                } else {
                    self->connect_tcp(results, req, req_msg, response,
                                      handler, start_time, buffer, res);
                }
            });
    }

    // A cached connection is dead if the server already sent EOF: it closed
    // the connection after the previous response (some servers do this without
    // advertising Connection: close). Writing to such a connection can block
    // indefinitely on Linux (a send on a socket whose peer sent FIN sits in
    // the kernel until the reset is processed), so the cached path detects the
    // EOF up front with a non-blocking peek and falls back to a fresh connect.
    // Would_block/try_again means no data AND no EOF/error: the connection is
    // still alive and safe to reuse.
    static bool cached_connection_dead(beast::tcp_stream& stream,
                                       beast::error_code& ec) {
        char c = 0;
        std::size_t n = stream.socket().receive(
            asio::buffer(&c, 1),
            asio::socket_base::message_peek, ec);
        if (ec == asio::error::would_block ||
            ec == asio::error::try_again) {
            ec = {};
            return false;
        }
        // Reset/not_connected/EOF (n == 0) or stray buffered bytes all mean
        // the connection cannot be safely reused.
        return true;
    }

    void request_on_cached(
        std::shared_ptr<ReusableConn> conn,
        const Request& req,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time)
    {
        auto self = shared_from_this();
        auto response = std::make_shared<Response>();
        auto res = std::make_shared<http::response<http::string_body>>();
        auto req_msg = build_request(req, response);
        if (!req_msg) {
            handler(response->ec, *response);
            return;
        }

        // Reject connections the server already closed instead of writing into
        // a dead socket (which can hang on Linux).
        beast::error_code peek_ec;
        bool dead = conn->tls
            ? cached_connection_dead(
                  beast::get_lowest_layer(*conn->ssl), peek_ec)
            : cached_connection_dead(*conn->tcp, peek_ec);
        if (dead) {
            drop_cached(conn);
            request_fresh(req, handler, start_time);
            return;
        }

        if (conn->tls) {
            write_read_cached_tls(conn, req, req_msg, response,
                                  handler, start_time, res);
        } else {
            write_read_cached_tcp(conn, req, req_msg, response,
                                  handler, start_time, res);
        }
    }

    // Reuse an existing plaintext connection. Any I/O failure means the
    // connection is gone (server closed it or it timed out), so it is dropped
    // from the cache and the request is transparently retried on a fresh one.
    void write_read_cached_tcp(
        std::shared_ptr<ReusableConn> conn,
        const Request& req,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();
        auto stream = conn->tcp;
        stream->expires_after(std::chrono::milliseconds(
            timeout_ms_.load(std::memory_order_relaxed)));
        http::async_write(*stream, *req_msg,
            [self, conn, stream, req, req_msg, response, handler, start_time, res](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    self->drop_cached(conn);
                    if (method_is_idempotent(req.method)) {
                        self->request_fresh(req, handler, start_time);
                    } else {
                        response->ec = Err::write_error;
                        handler(response->ec, *response);
                    }
                    return;
                }

                stream->expires_after(std::chrono::milliseconds(
                    self->timeout_ms_.load(std::memory_order_relaxed)));
                http::async_read(*stream, *conn->buffer, *res,
                    [self, conn, req, res, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        self->populate_response(response, res, ec, start_time);
                        if (ec) {
                            // The server closed a cached keep-alive connection
                            // before answering: retry once on a fresh
                            // connection (idempotent methods only) instead of
                            // failing the request. Genuine timeouts are not
                            // retried - the connection may still be alive.
                            if (ec != asio::error::timed_out &&
                                response->status_code == 0 &&
                                method_is_idempotent(req.method)) {
                                self->drop_cached(conn);
                                self->request_fresh(req, handler, start_time);
                                return;
                            }
                            self->drop_cached(conn);
                        }
                        handler(response->ec, *response);
                    });
            });
    }

    void write_read_cached_tls(
        std::shared_ptr<ReusableConn> conn,
        const Request& req,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();
        auto stream = conn->ssl;
        beast::get_lowest_layer(*stream).expires_after(std::chrono::milliseconds(
            timeout_ms_.load(std::memory_order_relaxed)));
        http::async_write(*stream, *req_msg,
            [self, conn, stream, req, req_msg, response, handler, start_time, res](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    self->drop_cached(conn);
                    if (method_is_idempotent(req.method)) {
                        self->request_fresh(req, handler, start_time);
                    } else {
                        response->ec = Err::write_error;
                        handler(response->ec, *response);
                    }
                    return;
                }

                beast::get_lowest_layer(*stream).expires_after(std::chrono::milliseconds(
                    self->timeout_ms_.load(std::memory_order_relaxed)));
                http::async_read(*stream, *conn->buffer, *res,
                    [self, conn, req, res, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        self->populate_response(response, res, ec, start_time);
                        if (ec) {
                            // Stale keep-alive: server closed before answering.
                            // Retry once on a fresh connection (idempotent
                            // methods only); real timeouts are not retried.
                            if (ec != asio::error::timed_out &&
                                response->status_code == 0 &&
                                method_is_idempotent(req.method)) {
                                self->drop_cached(conn);
                                self->request_fresh(req, handler, start_time);
                                return;
                            }
                            self->drop_cached(conn);
                        }
                        handler(response->ec, *response);
                    });
            });
    }

    void connect_tcp(
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
        auto stream = std::make_shared<beast::tcp_stream>(ioc_);
        auto ms = std::chrono::milliseconds(timeout_ms_.load(std::memory_order_relaxed));
        stream->expires_after(ms);

        stream->async_connect(results,
            [self, stream, req, req_msg, response, handler, start_time, buffer, res](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                self->send_and_receive(stream, req, req_msg, response, handler,
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
        auto ms = std::chrono::milliseconds(timeout_ms_.load(std::memory_order_relaxed));
        tcp_stream->expires_after(ms);

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

                // RFC 6066: SNI must not carry an IP literal. Resolvers and
                // the name-verification callback use the raw host instead.
                if (!host_is_ip_literal(req.host)) {
                    if (!SSL_set_tlsext_host_name(
                            ssl_stream->native_handle(), req.host.c_str())) {
                        response->ec = Err::tls_handshake_failed;
                        handler(response->ec, *response);
                        return;
                    }
                }

                // Enforce hostname verification on top of chain validation.
                // No-op when the context uses verify_none.
                if (self->tls_ctx_->is_verify_enabled()) {
                    ssl_stream->set_verify_callback(
                        asio::ssl::host_name_verification(req.host));
                }

                beast::get_lowest_layer(*ssl_stream).expires_after(
                    std::chrono::milliseconds(self->timeout_ms_.load(std::memory_order_relaxed)));
                ssl_stream->async_handshake(asio::ssl::stream_base::client,
                    [self, ssl_stream, req_msg, response, handler,
                     start_time, buffer, res, req](
                        beast::error_code ec)
                    {
                        if (ec) {
                            response->ec = Err::tls_handshake_failed;
                            handler(response->ec, *response);
                            return;
                        }

                        self->send_and_receive_tls(ssl_stream, req, req_msg,
                            response, handler, start_time, buffer, res);
                    });
            });
    }

    void populate_response(
        std::shared_ptr<Response> response,
        const std::shared_ptr<http::response<http::string_body>>& res,
        beast::error_code ec,
        std::chrono::steady_clock::time_point start_time)
    {
        auto end_time = std::chrono::steady_clock::now();
        response->latency =
            std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time);

        if (ec) {
            if (ec == http::error::end_of_stream) {
                response->status_code = static_cast<uint16_t>(res->result_int());
                response->body = res->body();
                for (const auto& field : *res) {
                    response->headers[std::string(field.name_string())] =
                        std::string(field.value());
                }
            } else {
                response->ec = Err::read_error;
            }
        } else {
            response->status_code = static_cast<uint16_t>(res->result_int());
            response->body = res->body();
            for (const auto& field : *res) {
                response->headers[std::string(field.name_string())] =
                    std::string(field.value());
            }
        }
    }

    void send_and_receive(
        std::shared_ptr<beast::tcp_stream> stream,
        const Request& req,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();

        stream->expires_after(std::chrono::milliseconds(
            timeout_ms_.load(std::memory_order_relaxed)));
        http::async_write(*stream, *req_msg,
            [self, stream, req, req_msg, response, handler, start_time, buffer, res](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                stream->expires_after(std::chrono::milliseconds(
                    self->timeout_ms_.load(std::memory_order_relaxed)));
                http::async_read(*stream, *buffer, *res,
                    [self, stream, req, buffer, res, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        self->populate_response(response, res, ec, start_time);
                        // A clean read means the server kept the connection
                        // open; cache it so the next request can reuse it.
                        if (self->keep_alive_.load(std::memory_order_relaxed) && !ec) {
                            self->cache_connection(stream, nullptr, buffer,
                                req.host, req.port, false);
                        }
                        handler(response->ec, *response);
                    });
            });
    }

    void send_and_receive_tls(
        std::shared_ptr<asio::ssl::stream<beast::tcp_stream>> ssl_stream,
        const Request& req,
        std::shared_ptr<http::request<http::string_body>> req_msg,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer,
        std::shared_ptr<http::response<http::string_body>> res)
    {
        auto self = shared_from_this();

        beast::get_lowest_layer(*ssl_stream).expires_after(std::chrono::milliseconds(
            timeout_ms_.load(std::memory_order_relaxed)));
        http::async_write(*ssl_stream, *req_msg,
            [self, ssl_stream, req, req_msg, response, handler, start_time, buffer, res](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                beast::get_lowest_layer(*ssl_stream).expires_after(std::chrono::milliseconds(
                    self->timeout_ms_.load(std::memory_order_relaxed)));
                http::async_read(*ssl_stream, *buffer, *res,
                    [self, ssl_stream, req, buffer, res, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        self->populate_response(response, res, ec, start_time);
                        if (self->keep_alive_.load(std::memory_order_relaxed) && !ec) {
                            self->cache_connection(nullptr, ssl_stream, buffer,
                                req.host, req.port, true);
                        }
                        handler(response->ec, *response);
                    });
            });
    }

    void cache_connection(
        std::shared_ptr<beast::tcp_stream> tcp,
        std::shared_ptr<asio::ssl::stream<beast::tcp_stream>> ssl,
        std::shared_ptr<beast::flat_buffer> buffer,
        const std::string& host,
        uint16_t port,
        bool tls)
    {
        auto conn = std::make_shared<ReusableConn>();
        conn->tcp = std::move(tcp);
        conn->ssl = std::move(ssl);
        conn->buffer = std::move(buffer);
        conn->host = host;
        conn->port = port;
        conn->tls = tls;
        cached_ = std::move(conn);
    }

    void drop_cached(std::shared_ptr<ReusableConn> conn) {
        if (cached_ == conn) cached_.reset();
    }

    asio::io_context& ioc_;
    std::atomic<int64_t> timeout_ms_{core::kDefaultTimeout.count()};
    std::atomic<bool> keep_alive_{true};
    std::unique_ptr<security::TlsContext> tls_ctx_;
    std::shared_ptr<ReusableConn> cached_;
};

Http11Client::Http11Client(
    asio::io_context& ioc,
    const security::TlsConfig& tls_config,
    bool keep_alive)
    : impl_(std::make_shared<Impl>(ioc, tls_config, keep_alive))
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
