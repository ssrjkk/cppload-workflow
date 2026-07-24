// @author ssrjkk | cppload
#include "cppload/net/connection.hpp"
#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <memory>

namespace beast = boost::beast;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace cppload::net {

//
// TcpConnection
//

TcpConnection::TcpConnection(asio::io_context& ioc)
    : stream_(ioc)
{
}

void TcpConnection::async_write(
    asio::const_buffer buffer,
    std::function<void(std::error_code, size_t)> handler)
{
    stream_.expires_after(timeout_);
    asio::async_write(stream_, buffer,
        [handler](beast::error_code ec, size_t n) {
            handler(ec == asio::error::operation_aborted ? Err::operation_cancelled : ec, n);
        });
}

void TcpConnection::async_read_some(
    asio::mutable_buffer buffer,
    std::function<void(std::error_code, size_t)> handler)
{
    stream_.expires_after(timeout_);
    stream_.async_read_some(buffer,
        [handler](beast::error_code ec, size_t n) {
            if (ec == asio::error::operation_aborted) {
                handler(Err::operation_cancelled, n);
            } else {
                handler(ec, n);
            }
        });
}

void TcpConnection::close() {
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
    stream_.socket().close(ec);
}

void TcpConnection::set_timeout(std::chrono::milliseconds ms) {
    timeout_ = ms;
}

bool TcpConnection::is_open() const {
    return stream_.socket().is_open();
}

std::string TcpConnection::remote_address() const {
    beast::error_code ec;
    auto ep = stream_.socket().remote_endpoint(ec);
    return ec ? "" : ep.address().to_string();
}

uint16_t TcpConnection::remote_port() const {
    beast::error_code ec;
    auto ep = stream_.socket().remote_endpoint(ec);
    return ec ? 0 : ep.port();
}

//
// SslConnection
//

SslConnection::SslConnection(
    asio::io_context& ioc,
    asio::ssl::context& ssl_ctx)
    : ssl_stream_(beast::tcp_stream(ioc), ssl_ctx)
{
}

void SslConnection::async_write(
    asio::const_buffer buffer,
    std::function<void(std::error_code, size_t)> handler)
{
    beast::get_lowest_layer(ssl_stream_).expires_after(timeout_);
    asio::async_write(ssl_stream_, buffer,
        [handler](beast::error_code ec, size_t n) {
            handler(ec == asio::error::operation_aborted ? Err::operation_cancelled : ec, n);
        });
}

void SslConnection::async_read_some(
    asio::mutable_buffer buffer,
    std::function<void(std::error_code, size_t)> handler)
{
    beast::get_lowest_layer(ssl_stream_).expires_after(timeout_);
    ssl_stream_.async_read_some(buffer,
        [handler](beast::error_code ec, size_t n) {
            if (ec == asio::error::operation_aborted) {
                handler(Err::operation_cancelled, n);
            } else {
                handler(ec, n);
            }
        });
}

void SslConnection::async_handshake(
    std::function<void(std::error_code)> handler)
{
    beast::get_lowest_layer(ssl_stream_).expires_after(timeout_);
    ssl_stream_.async_handshake(asio::ssl::stream_base::client,
        [handler](beast::error_code ec) {
            if (ec) {
                handler(Err::tls_handshake_failed);
            } else {
                handler({});
            }
        });
}

void SslConnection::close() {
    beast::error_code ec;
    ssl_stream_.shutdown(ec);
    beast::get_lowest_layer(ssl_stream_).socket().close(ec);
}

void SslConnection::set_timeout(std::chrono::milliseconds ms) {
    timeout_ = ms;
}

bool SslConnection::is_open() const {
    return beast::get_lowest_layer(ssl_stream_).socket().is_open();
}

std::string SslConnection::remote_address() const {
    beast::error_code ec;
    auto ep = beast::get_lowest_layer(ssl_stream_).socket().remote_endpoint(ec);
    return ec ? "" : ep.address().to_string();
}

uint16_t SslConnection::remote_port() const {
    beast::error_code ec;
    auto ep = beast::get_lowest_layer(ssl_stream_).socket().remote_endpoint(ec);
    return ec ? 0 : ep.port();
}

//
// TcpConnector
//

TcpConnector::TcpConnector(asio::io_context& ioc)
    : ioc_(ioc)
{
}

void TcpConnector::async_connect(
    const std::string& host,
    uint16_t port,
    std::function<void(std::error_code, std::unique_ptr<Connection>)> handler)
{
    auto resolver = std::make_shared<tcp::resolver>(ioc_);

    auto ioc_ptr = &ioc_;
    resolver->async_resolve(host, std::to_string(port),
        [resolver, timeout = timeout_, handler, ioc_ptr](
            beast::error_code ec, tcp::resolver::results_type results) mutable
        {
            if (ec) {
                std::error_code err = (ec == asio::error::host_not_found)
                    ? Err::dns_failure : Err::connection_refused;
                handler(err, nullptr);
                return;
            }

            auto conn = std::make_unique<TcpConnection>(*ioc_ptr);
            auto& stream = conn->stream();
            stream.expires_after(timeout);
            stream.async_connect(results,
                [conn = std::move(conn), handler](
                    beast::error_code ec, const tcp::endpoint& ep) mutable
                {
                    if (ec) {
                        std::error_code err = (ec == asio::error::timed_out)
                            ? Err::connection_timeout : Err::connection_refused;
                        handler(err, nullptr);
                        return;
                    }
                    handler({}, std::move(conn));
                });
        });
}

void TcpConnector::set_timeout(std::chrono::milliseconds ms) {
    timeout_ = ms;
}

//
// SslConnector
//

SslConnector::SslConnector(
    asio::io_context& ioc,
    asio::ssl::context& ssl_ctx)
    : ioc_(ioc), ssl_ctx_(ssl_ctx)
{
}

void SslConnector::async_connect(
    const std::string& host,
    uint16_t port,
    std::function<void(std::error_code, std::unique_ptr<Connection>)> handler)
{
    auto resolver = std::make_shared<tcp::resolver>(ioc_);

    auto ioc_ptr = &ioc_;
    auto ssl_ctx_ptr = &ssl_ctx_;
    resolver->async_resolve(host, std::to_string(port),
        [resolver, handler, host, timeout = timeout_, ioc_ptr, ssl_ctx_ptr](
            beast::error_code ec, tcp::resolver::results_type results) mutable
        {
            if (ec) {
                std::error_code err = (ec == asio::error::host_not_found)
                    ? Err::dns_failure : Err::connection_refused;
                handler(err, nullptr);
                return;
            }

            auto conn = std::make_unique<SslConnection>(*ioc_ptr, *ssl_ctx_ptr);
            auto& ssl_stream = conn->stream();
            auto& tcp_stream = beast::get_lowest_layer(ssl_stream);
            tcp_stream.expires_after(timeout);

            tcp_stream.async_connect(results,
                [conn = std::move(conn), handler, host, timeout](
                    beast::error_code ec,
                    const tcp::endpoint&) mutable
                {
                    if (ec) {
                        std::error_code err = (ec == asio::error::timed_out)
                            ? Err::connection_timeout : Err::connection_refused;
                        handler(err, nullptr);
                        return;
                    }

                    auto& ssl_stream = conn->stream();
                    auto& tcp_stream = beast::get_lowest_layer(ssl_stream);

                    // Set SNI hostname
                    if (!SSL_set_tlsext_host_name(
                            ssl_stream.native_handle(), host.c_str()))
                    {
                        handler(Err::tls_handshake_failed, nullptr);
                        return;
                    }

                    tcp_stream.expires_after(timeout);
                    ssl_stream.async_handshake(asio::ssl::stream_base::client,
                        [conn = std::move(conn), handler](
                            beast::error_code ec) mutable
                        {
                            if (ec) {
                                handler(Err::tls_handshake_failed, nullptr);
                                return;
                            }
                            handler({}, std::move(conn));
                        });
                });
        });
}

void SslConnector::set_timeout(std::chrono::milliseconds ms) {
    timeout_ = ms;
}

} // namespace cppload::net