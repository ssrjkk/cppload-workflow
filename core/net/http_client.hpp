#pragma once
#include <boost/asio/io_context.hpp>
#include <functional>
#include <string>

namespace cppload::net {

class HttpClientImpl;

class HttpClient {
public:
    explicit HttpClient(boost::asio::io_context& ioc);
    ~HttpClient();
    
    void async_request(const std::string& host, const std::string& target,
                      std::function<void(const std::string&)> callback);
    
private:
    std::unique_ptr<HttpClientImpl> impl_;
};

} // namespace cppload::net
