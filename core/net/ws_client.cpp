// @author ssrjkk | cppload
#include "cppload/net/ws_client.hpp"
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <atomic>
#include <memory>
#include <chrono>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;

namespace cppload::net {

static bool host_is_ip_literal(const std::string& host) {
    if (host.find(':') != std::string::npos) return true;
    if (host.empty()) return false;
    for (char c : host) {
        if (!(c == '.' || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

class WsClient::Impl : public std::enable_shared_from_this<Impl> {
public:
    Impl(asio::io_context& ioc, const security::TlsConfig& tls_config)
        : ioc_(ioc), timeout_ms_(5000)
    {
        tls_ctx_ = std::make_unique<security::TlsContext>(tls_config);
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
        auto resolver = std::make_shared<asio::ip::tcp::resolver>(ioc_);

        resolver->async_resolve(req.host, std::to_string(req.port),
            [self, resolver, response, handler, start_time, req, use_tls](
                beast::error_code ec, asio::ip::tcp::resolver::results_type results)
            {
                if (ec) {
                    response->ec = (ec == asio::error::host_not_found)
                        ? Err::dns_failure : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                if (use_tls) {
                    self->connect_wss(results, req, response, handler, start_time);
                } else {
                    self->connect_ws(results, req, response, handler, start_time);
                }
            });
    }

    void set_timeout(std::chrono::milliseconds ms) {
        timeout_ms_.store(ms.count(), std::memory_order_relaxed);
    }

private:
    void connect_ws(
        asio::ip::tcp::resolver::results_type results,
        const Request& req,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time)
    {
        auto self = shared_from_this();
        auto ws = std::make_shared<websocket::stream<beast::tcp_stream>>(ioc_);

        beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(timeout_ms_.load(std::memory_order_relaxed)));
        beast::get_lowest_layer(*ws).async_connect(results,
            [self, ws, response, handler, start_time, req](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(self->timeout_ms_.load(std::memory_order_relaxed)));
                std::string target = req.path.empty() ? "/" : req.path;
                ws->async_handshake(req.host, target,
                    [self, ws, response, handler, start_time, req](
                        beast::error_code ec)
                    {
                        if (ec) {
                            response->ec = Err::protocol_violation;
                            handler(response->ec, *response);
                            return;
                        }

                        self->send_message(ws, req.body, response, handler, start_time);
                    });
            });
    }

    void connect_wss(
        asio::ip::tcp::resolver::results_type results,
        const Request& req,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time)
    {
        auto self = shared_from_this();
        auto ws = std::make_shared<
            websocket::stream<asio::ssl::stream<beast::tcp_stream>>>(
                ioc_, self->tls_ctx_->get_native_context());

        beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(timeout_ms_.load(std::memory_order_relaxed)));
        beast::get_lowest_layer(*ws).async_connect(results,
            [self, ws, response, handler, start_time, req](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                if (!host_is_ip_literal(req.host) && !SSL_set_tlsext_host_name(
                        ws->next_layer().native_handle(), req.host.c_str())) {
                    response->ec = Err::tls_handshake_failed;
                    handler(response->ec, *response);
                    return;
                }

                // Enforce hostname verification on top of chain validation.
                // No-op when the context uses verify_none.
                if (self->tls_ctx_->is_verify_enabled()) {
                    ws->next_layer().set_verify_callback(
                        asio::ssl::host_name_verification(req.host));
                }

                beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(self->timeout_ms_.load(std::memory_order_relaxed)));
                ws->next_layer().async_handshake(asio::ssl::stream_base::client,
                    [self, ws, response, handler, start_time, req](
                        beast::error_code ec)
                    {
                        if (ec) {
                            response->ec = Err::tls_handshake_failed;
                            handler(response->ec, *response);
                            return;
                        }

                        std::string target = req.path.empty() ? "/" : req.path;
                        ws->async_handshake(req.host, target,
                            [self, ws, response, handler, start_time, req](
                                beast::error_code ec)
                            {
                                if (ec) {
                                    response->ec = Err::protocol_violation;
                                    handler(response->ec, *response);
                                    return;
                                }

                                self->send_message_tls(ws, req.body, response,
                                                       handler, start_time);
                            });
                    });
            });
    }

    void populate_response(
        std::shared_ptr<Response> response,
        std::shared_ptr<beast::flat_buffer> recv_buf,
        beast::error_code ec,
        std::chrono::steady_clock::time_point start_time)
    {
        auto end_time = std::chrono::steady_clock::now();
        response->latency =
            std::chrono::duration_cast<std::chrono::microseconds>(
                end_time - start_time);

        if (ec) {
            response->ec = Err::read_error;
        } else {
            response->body.assign(
                reinterpret_cast<const char*>(recv_buf->data().data()),
                recv_buf->data().size());
            response->status_code = 1;
        }
    }

    void send_message(
        std::shared_ptr<websocket::stream<beast::tcp_stream>> ws,
        const std::string& body,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time)
    {
        auto self = shared_from_this();
        auto send_buf = std::make_shared<std::string>(body);

        beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(timeout_ms_.load(std::memory_order_relaxed)));
        ws->async_write(asio::buffer(*send_buf),
            [self, ws, send_buf, response, handler, start_time](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                auto recv_buf = std::make_shared<beast::flat_buffer>();
                beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(self->timeout_ms_.load(std::memory_order_relaxed)));
                ws->async_read(*recv_buf,
                    [self, ws, recv_buf, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        self->populate_response(response, recv_buf, ec, start_time);
                        handler(response->ec, *response);
                    });
            });
    }

    void send_message_tls(
        std::shared_ptr<websocket::stream<asio::ssl::stream<beast::tcp_stream>>> ws,
        const std::string& body,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time)
    {
        auto self = shared_from_this();
        auto send_buf = std::make_shared<std::string>(body);

        beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(timeout_ms_.load(std::memory_order_relaxed)));
        ws->async_write(asio::buffer(*send_buf),
            [self, ws, send_buf, response, handler, start_time](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                auto recv_buf = std::make_shared<beast::flat_buffer>();
                beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(self->timeout_ms_.load(std::memory_order_relaxed)));
                ws->async_read(*recv_buf,
                    [self, ws, recv_buf, response, handler, start_time](
                        beast::error_code ec, std::size_t)
                    {
                        self->populate_response(response, recv_buf, ec, start_time);
                        handler(response->ec, *response);
                    });
            });
    }

    asio::io_context& ioc_;
    std::atomic<int64_t> timeout_ms_;
    std::unique_ptr<security::TlsContext> tls_ctx_;
};

WsClient::WsClient(
    asio::io_context& ioc,
    const security::TlsConfig& tls_config)
    : impl_(std::make_shared<Impl>(ioc, tls_config))
{
}

WsClient::~WsClient() = default;

WsClient::WsClient(WsClient&&) noexcept = default;
WsClient& WsClient::operator=(WsClient&&) noexcept = default;

void WsClient::async_request(
    const Request& req,
    std::function<void(std::error_code, Response)> handler)
{
    impl_->async_request(req, handler);
}

void WsClient::set_timeout(std::chrono::milliseconds ms) {
    impl_->set_timeout(ms);
}

} // namespace cppload::net