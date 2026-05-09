#include "cppload/net/http_client.hpp"
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <memory>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace net = cppload::net;

class net::HttpClient::Impl : public std::enable_shared_from_this<Impl> {
public:
    explicit Impl(asio::io_context& ioc)
        : ioc_(ioc), timeout_(5000), keep_alive_(true) {}

    void async_request(const HttpRequest& req,
                      std::function<void(const HttpResponse&)> callback) {
        auto start_time = std::chrono::steady_clock::now();
        auto self = shared_from_this();

        auto resolver = std::make_shared<asio::ip::tcp::resolver>(ioc_);
        auto stream = std::make_shared<beast::tcp_stream>(ioc_);
        auto response = std::make_shared<HttpResponse>();
        auto req_msg = std::make_shared<http::request<http::string_body>>();
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto res = std::make_shared<http::response<http::string_body>>();

        req_msg->method_string(http::string_to_verb(req.method));
        req_msg->target(req.target);
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

        stream->expires_after(timeout_);
        resolver->async_resolve(req.host, req.port,
            [self, resolver, stream, req_msg, response, callback, start_time, buffer, res](
                beast::error_code ec, asio::ip::tcp::resolver::results_type results) {

                if (ec) {
                    HttpResponse err_resp;
                    err_resp.status_code = 0;
                    callback(err_resp);
                    return;
                }

                stream->expires_after(self->timeout_);
                stream->async_connect(results,
                    [self, stream, req_msg, response, callback, start_time, buffer, res](
                        beast::error_code ec, const asio::ip::tcp::endpoint&) {

                        if (ec) {
                            HttpResponse err_resp;
                            err_resp.status_code = 0;
                            callback(err_resp);
                            return;
                        }

                        stream->expires_after(self->timeout_);
                        http::async_write(*stream, *req_msg,
                            [self, stream, req_msg, response, callback, start_time, buffer, res](
                                beast::error_code ec, std::size_t) {

                                if (ec) {
                                    HttpResponse err_resp;
                                    err_resp.status_code = 0;
                                    callback(err_resp);
                                    return;
                                }

                                stream->expires_after(self->timeout_);
                                http::async_read(*stream, *buffer, *res,
                                    [self, stream, buffer, res, response, callback, start_time](
                                        beast::error_code ec, std::size_t) {

                                        auto end_time = std::chrono::steady_clock::now();

                                        if (ec && ec != beast::http::error::end_of_stream) {
                                            response->status_code = 0;
                                        } else {
                                            response->status_code = res->result_int();
                                            response->body = res->body();
                                            response->latency =
                                                std::chrono::duration_cast<std::chrono::microseconds>(
                                                    end_time - start_time);

                                            for (const auto& field : *res) {
                                                response->headers[std::string(field.name_string())] =
                                                    std::string(field.value());
                                            }
                                        }

                                        if (!self->keep_alive_) {
                                            beast::error_code shutdown_ec;
                                            stream->socket().shutdown(asio::ip::tcp::socket::shutdown_both, shutdown_ec);
                                        }

                                        callback(*response);
                                    });
                            });
                    });
            });
    }

    void set_timeout(std::chrono::milliseconds timeout) { timeout_ = timeout; }
    void set_keep_alive(bool keep_alive) { keep_alive_ = keep_alive; }

private:
    asio::io_context& ioc_;
    std::chrono::milliseconds timeout_;
    bool keep_alive_;
};

net::HttpClient::HttpClient(asio::io_context& ioc) 
    : impl_(std::make_shared<Impl>(ioc)) {}

net::HttpClient::~HttpClient() = default;

net::HttpClient::HttpClient(HttpClient&&) noexcept = default;
net::HttpClient& net::HttpClient::operator=(HttpClient&&) noexcept = default;

void net::HttpClient::async_request(const HttpRequest& req, 
                                   std::function<void(const HttpResponse&)> callback) {
    impl_->async_request(req, callback);
}

void net::HttpClient::set_timeout(std::chrono::milliseconds timeout) {
    impl_->set_timeout(timeout);
}

void net::HttpClient::set_keep_alive(bool keep_alive) {
    impl_->set_keep_alive(keep_alive);
}
