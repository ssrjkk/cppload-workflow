// @author ssrjkk | volley
#pragma once

#include "cppload/platform/policy.hpp"
#include "cppload/platform/repository.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <string>
#include <thread>

namespace cppload::platform {

class HttpServer final {
public:
    HttpServer(std::shared_ptr<Repository> repo,
               std::shared_ptr<PolicyEvaluator> evaluator,
               std::string address = "0.0.0.0",
               uint16_t port = 8080);

    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();

private:
    using http_response = boost::beast::http::response<boost::beast::http::string_body>;

    void accept_loop();
    void handle_session(boost::asio::ip::tcp::socket socket);

    auto route(boost::beast::http::verb method, const std::string& path,
               const std::string& body) -> http_response;

    auto handle_projects(boost::beast::http::verb method,
                         const std::string& body) -> http_response;
    auto handle_project(boost::beast::http::verb method, const std::string& id,
                        const std::string& body) -> http_response;
    auto handle_scenarios(boost::beast::http::verb method, const std::string& project_id,
                          const std::string& body) -> http_response;
    auto handle_runs(boost::beast::http::verb method, const std::string& project_id,
                     const std::string& body) -> http_response;
    auto handle_policies(boost::beast::http::verb method, const std::string& project_id,
                         const std::string& body) -> http_response;
    auto handle_evaluate(boost::beast::http::verb method,
                         const std::string& body) -> http_response;

    std::shared_ptr<Repository> repo_;
    std::shared_ptr<PolicyEvaluator> evaluator_;
    std::string address_;
    uint16_t port_;
    boost::asio::io_context ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread accept_thread_;
    bool running_ = false;
};

} // namespace cppload::platform
