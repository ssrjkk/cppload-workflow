#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace cppload::net {

struct HttpResponse {
    unsigned int status_code{0};
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::chrono::microseconds latency;
};

struct HttpRequest {
    std::string method;
    std::string target;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string host;
    std::string port{"80"};
};

class HttpClient {
public:
    explicit HttpClient(boost::asio::io_context& ioc);
    ~HttpClient();
    
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;
    
    void async_request(const HttpRequest& req, 
                      std::function<void(const HttpResponse&)> callback);
    
    void set_timeout(std::chrono::milliseconds timeout);
    void set_keep_alive(bool keep_alive);
    
private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace cppload::net
