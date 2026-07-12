#pragma once

#include "cppload/error.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace cppload::net {

class Connection {
public:
    virtual ~Connection() = default;

    virtual void async_write(
        boost::asio::const_buffer buffer,
        std::function<void(std::error_code, size_t)> handler) = 0;

    virtual void async_read_some(
        boost::asio::mutable_buffer buffer,
        std::function<void(std::error_code, size_t)> handler) = 0;

    virtual void close() = 0;

    virtual void set_timeout(std::chrono::milliseconds ms) = 0;

    virtual bool is_open() const = 0;

    virtual std::string remote_address() const { return {}; }
    virtual uint16_t remote_port() const { return 0; }
};

class TcpConnection final : public Connection {
public:
    explicit TcpConnection(boost::asio::io_context& ioc);

    void async_write(
        boost::asio::const_buffer buffer,
        std::function<void(std::error_code, size_t)> handler) override;

    void async_read_some(
        boost::asio::mutable_buffer buffer,
        std::function<void(std::error_code, size_t)> handler) override;

    void close() override;

    void set_timeout(std::chrono::milliseconds ms) override;

    bool is_open() const override;

    std::string remote_address() const override;
    uint16_t remote_port() const override;

    boost::beast::tcp_stream& stream() { return stream_; }

private:
    boost::beast::tcp_stream stream_;
    std::chrono::milliseconds timeout_{5000};
};

class SslConnection final : public Connection {
public:
    SslConnection(
        boost::asio::io_context& ioc,
        boost::asio::ssl::context& ssl_ctx);

    void async_write(
        boost::asio::const_buffer buffer,
        std::function<void(std::error_code, size_t)> handler) override;

    void async_read_some(
        boost::asio::mutable_buffer buffer,
        std::function<void(std::error_code, size_t)> handler) override;

    void close() override;

    void set_timeout(std::chrono::milliseconds ms) override;

    bool is_open() const override;

    std::string remote_address() const override;
    uint16_t remote_port() const override;

    void async_handshake(
        std::function<void(std::error_code)> handler);

    boost::asio::ssl::stream<boost::beast::tcp_stream>& stream() { return ssl_stream_; }

private:
    boost::asio::ssl::stream<boost::beast::tcp_stream> ssl_stream_;
    std::chrono::milliseconds timeout_{5000};
};

class Connector {
public:
    virtual ~Connector() = default;

    virtual void async_connect(
        const std::string& host,
        uint16_t port,
        std::function<void(std::error_code, std::unique_ptr<Connection>)> handler) = 0;

    virtual void set_timeout(std::chrono::milliseconds ms) = 0;
};

class TcpConnector final : public Connector {
public:
    explicit TcpConnector(boost::asio::io_context& ioc);

    void async_connect(
        const std::string& host,
        uint16_t port,
        std::function<void(std::error_code, std::unique_ptr<Connection>)> handler) override;

    void set_timeout(std::chrono::milliseconds ms) override;

private:
    boost::asio::io_context& ioc_;
    std::chrono::milliseconds timeout_{5000};
};

class SslConnector final : public Connector {
public:
    SslConnector(
        boost::asio::io_context& ioc,
        boost::asio::ssl::context& ssl_ctx);

    void async_connect(
        const std::string& host,
        uint16_t port,
        std::function<void(std::error_code, std::unique_ptr<Connection>)> handler) override;

    void set_timeout(std::chrono::milliseconds ms) override;

private:
    boost::asio::io_context& ioc_;
    boost::asio::ssl::context& ssl_ctx_;
    std::chrono::milliseconds timeout_{5000};
};

} // namespace cppload::net
