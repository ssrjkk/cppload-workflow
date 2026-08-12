// @author ssrjkk | cppload
#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

class MockHttpServer {
public:
    using Handler = std::function<http::response<http::string_body>(
        const http::request<http::string_body>&)>;

    explicit MockHttpServer(uint16_t port = 0, const std::string& address = "127.0.0.1")
        : port_(port)
        , address_(address)
        , acceptor_(ioc_)
        , running_(false)
    {
    }

    ~MockHttpServer() { stop(); }

    MockHttpServer(const MockHttpServer&) = delete;
    MockHttpServer& operator=(const MockHttpServer&) = delete;

    bool start() {
        try {
            auto address = asio::ip::make_address(address_);
            asio::ip::tcp::endpoint ep(address, port_);
            acceptor_.open(ep.protocol());
            acceptor_.set_option(asio::socket_base::reuse_address(true));
            acceptor_.bind(ep);
            acceptor_.listen();
            if (port_ == 0) port_ = acceptor_.local_endpoint().port();
            running_ = true;
            thread_ = std::thread([this]() { run(); });
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void stop() {
        running_ = false;
        beast::error_code ec;
        acceptor_.close(ec);
        if (thread_.joinable()) thread_.join();
        for (auto& t : sessions_) {
            if (t.joinable()) t.join();
        }
        sessions_.clear();
    }

    uint16_t port() const { return port_; }

    void set_handler(Handler handler) { handler_ = std::move(handler); }

private:
    // Accept loop that must be interruptible from stop(): closing a listener
    // does NOT wake a thread blocked in a blocking accept() on Linux, which
    // made ~MockHttpServer() deadlock. A non-blocking acceptor returns
    // would_block immediately, so the loop wakes up every few ms and can exit
    // as soon as running_ flips.
    void run() {
        beast::error_code ec;
        acceptor_.non_blocking(true, ec);
        while (running_) {
            asio::ip::tcp::socket socket(ioc_);
            ec.clear();
            acceptor_.accept(socket, ec);
            if (ec == asio::error::would_block ||
                ec == asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (ec || !running_) break;
            socket.non_blocking(false, ec);
            {
                std::lock_guard<std::mutex> lock(sessions_mutex_);
                sessions_.emplace_back([this, s = std::move(socket)]() mutable {
                    handle_session(std::move(s));
                });
            }
        }
    }

    void handle_session(asio::ip::tcp::socket socket) {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        beast::error_code ec;

        http::read(socket, buffer, req, ec);
        if (ec) return;

        http::response<http::string_body> res;
        try {
            if (handler_) {
                res = handler_(req);
            } else {
                res.result(http::status::ok);
                res.set(http::field::content_type, "text/plain");
                res.body() = "OK";
                res.prepare_payload();
            }
        } catch (const std::exception&) {
            res.result(http::status::internal_server_error);
            res.set(http::field::content_type, "text/plain");
            res.body() = "handler error";
            res.prepare_payload();
        }

        http::write(socket, res, ec);
        beast::error_code shut_ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_both, shut_ec);
    }

    asio::io_context ioc_;
    uint16_t port_;
    std::string address_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::atomic<bool> running_;
    Handler handler_;
    std::vector<std::thread> sessions_;
    std::mutex sessions_mutex_;
};