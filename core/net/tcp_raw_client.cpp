#include "cppload/net/tcp_raw_client.hpp"
#include "cppload/net/connection.hpp"
#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <memory>
#include <chrono>
#include <atomic>

namespace beast = boost::beast;
namespace asio = boost::asio;

namespace cppload::net {

class TcpRawClient::Impl : public std::enable_shared_from_this<Impl> {
public:
    Impl(asio::io_context& ioc, const security::TlsConfig& tls_config)
        : ioc_(ioc)
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
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto resolver = std::make_shared<asio::ip::tcp::resolver>(ioc_);

        resolver->async_resolve(req.host, std::to_string(req.port),
            [self, resolver, response, handler, start_time, buffer, req, use_tls](
                beast::error_code ec, asio::ip::tcp::resolver::results_type results)
            {
                if (ec) {
                    response->ec = (ec == asio::error::host_not_found)
                        ? Err::dns_failure : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                if (use_tls) {
                    self->connect_tls(results, req, response, handler, start_time, buffer);
                } else {
                    self->connect_tcp(results, req, response, handler, start_time, buffer);
                }
            });
    }

    void set_timeout(std::chrono::milliseconds ms) {
        timeout_ms_.store(ms.count(), std::memory_order_relaxed);
    }

private:
    void connect_tcp(
        asio::ip::tcp::resolver::results_type results,
        const Request& req,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer)
    {
        auto self = shared_from_this();
        auto stream = std::make_shared<beast::tcp_stream>(ioc_);
        stream->expires_after(get_timeout(timeout_ms_));

        stream->async_connect(results,
            [self, stream, response, handler, start_time, buffer, req](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                self->send_raw(stream, req.body, response, handler, start_time, buffer);
            });
    }

    void connect_tls(
        asio::ip::tcp::resolver::results_type results,
        const Request& req,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer)
    {
        auto self = shared_from_this();
        auto tcp_stream = std::make_shared<beast::tcp_stream>(ioc_);
        tcp_stream->expires_after(get_timeout(timeout_ms_));

        tcp_stream->async_connect(results,
            [self, tcp_stream, response, handler, start_time, buffer, req](
                beast::error_code ec, const asio::ip::tcp::endpoint&)
            {
                if (ec) {
                    response->ec = (ec == asio::error::timed_out)
                        ? Err::connection_timeout : Err::connection_refused;
                    handler(response->ec, *response);
                    return;
                }

                auto ssl_stream = std::make_shared<
                    asio::ssl::stream<beast::tcp_stream>>(
                        std::move(*tcp_stream),
                        self->tls_ctx_->get_native_context());

                if (!SSL_set_tlsext_host_name(
                        ssl_stream->native_handle(), req.host.c_str())) {
                    response->ec = Err::tls_handshake_failed;
                    handler(response->ec, *response);
                    return;
                }

                beast::get_lowest_layer(*ssl_stream).expires_after(self->get_timeout(self->timeout_ms_));
                ssl_stream->async_handshake(asio::ssl::stream_base::client,
                    [self, ssl_stream, response, handler, start_time, buffer, req](
                        beast::error_code ec)
                    {
                        if (ec) {
                            response->ec = Err::tls_handshake_failed;
                            handler(response->ec, *response);
                            return;
                        }

                        self->send_raw_tls(ssl_stream, req.body, response,
                                           handler, start_time, buffer);
                    });
            });
    }

    void send_raw(
        std::shared_ptr<beast::tcp_stream> stream,
        const std::string& body,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer)
    {
        auto self = shared_from_this();
        auto send_buf = std::make_shared<beast::flat_buffer>();
        boost::asio::buffer_copy(
            send_buf->prepare(body.size()),
            boost::asio::buffer(body));
        send_buf->commit(body.size());

        stream->expires_after(get_timeout(timeout_ms_));
        boost::asio::async_write(*stream, send_buf->data(),
            [self, stream, send_buf, response, handler, start_time, buffer](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                stream->expires_after(self->get_timeout(self->timeout_ms_));
                boost::asio::async_read(*stream, *buffer,
                    boost::asio::transfer_at_least(1),
                    [self, stream, buffer, response, handler, start_time](
                        beast::error_code ec, std::size_t bytes)
                    {
                        auto end_time = std::chrono::steady_clock::now();
                        response->latency =
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                end_time - start_time);

                        if (ec) {
                            response->ec = (ec == beast::error::timeout)
                                ? Err::timeout : Err::read_error;
                        } else {
                            response->body.assign(
                                reinterpret_cast<const char*>(buffer->data().data()),
                                buffer->data().size());
                            response->status_code = 1;
                        }

                        handler(response->ec, *response);
                    });
            });
    }

    void send_raw_tls(
        std::shared_ptr<asio::ssl::stream<beast::tcp_stream>> ssl_stream,
        const std::string& body,
        std::shared_ptr<Response> response,
        std::function<void(std::error_code, Response)> handler,
        std::chrono::steady_clock::time_point start_time,
        std::shared_ptr<beast::flat_buffer> buffer)
    {
        auto self = shared_from_this();
        auto send_buf = std::make_shared<beast::flat_buffer>();
        boost::asio::buffer_copy(
            send_buf->prepare(body.size()),
            boost::asio::buffer(body));
        send_buf->commit(body.size());

        beast::get_lowest_layer(*ssl_stream).expires_after(get_timeout(timeout_ms_));
        boost::asio::async_write(*ssl_stream, send_buf->data(),
            [self, ssl_stream, send_buf, response, handler, start_time, buffer](
                beast::error_code ec, std::size_t)
            {
                if (ec) {
                    response->ec = Err::write_error;
                    handler(response->ec, *response);
                    return;
                }

                beast::get_lowest_layer(*ssl_stream).expires_after(self->get_timeout(self->timeout_ms_));
                boost::asio::async_read(*ssl_stream, *buffer,
                    boost::asio::transfer_at_least(1),
                    [self, ssl_stream, buffer, response, handler, start_time](
                        beast::error_code ec, std::size_t bytes)
                    {
                        auto end_time = std::chrono::steady_clock::now();
                        response->latency =
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                end_time - start_time);

                        if (ec) {
                            response->ec = (ec == beast::error::timeout)
                                ? Err::timeout : Err::read_error;
                        } else {
                            response->body.assign(
                                reinterpret_cast<const char*>(buffer->data().data()),
                                buffer->data().size());
                            response->status_code = 1;
                        }

                        handler(response->ec, *response);
                    });
            });
    }

    static std::chrono::milliseconds get_timeout(const std::atomic<int64_t>& ms) {
        return std::chrono::milliseconds(ms.load(std::memory_order_relaxed));
    }

    asio::io_context& ioc_;
    std::atomic<int64_t> timeout_ms_{5000};
    std::unique_ptr<security::TlsContext> tls_ctx_;
};

TcpRawClient::TcpRawClient(
    asio::io_context& ioc,
    const security::TlsConfig& tls_config)
    : impl_(std::make_shared<Impl>(ioc, tls_config))
{
}

TcpRawClient::~TcpRawClient() = default;

TcpRawClient::TcpRawClient(TcpRawClient&&) noexcept = default;
TcpRawClient& TcpRawClient::operator=(TcpRawClient&&) noexcept = default;

void TcpRawClient::async_request(
    const Request& req,
    std::function<void(std::error_code, Response)> handler)
{
    impl_->async_request(req, handler);
}

void TcpRawClient::set_timeout(std::chrono::milliseconds ms) {
    impl_->set_timeout(ms);
}

} // namespace cppload::net
