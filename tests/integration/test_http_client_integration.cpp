#include <gtest/gtest.h>
#include "cppload/net/http_client.hpp"
#include "mock_server.hpp"
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <thread>

TEST(HttpClientIntegrationTest, SuccessfulRequest) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        EXPECT_EQ(req.method(), http::verb::get);
        EXPECT_EQ(req.target(), "/api/test");
        http::response<http::string_body> res;
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"status":"ok"})";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::HttpRequest req;
    req.method = "GET";
    req.target = "/api/test";
    req.host = "127.0.0.1";
    req.port = std::to_string(server.port());

    std::atomic<bool> called{false};
    client.async_request(req, [&](const cppload::net::HttpResponse& resp) {
        EXPECT_EQ(resp.status_code, 200);
        EXPECT_EQ(resp.body, R"({"status":"ok"})");
        EXPECT_GT(resp.latency.count(), 0);
        called = true;
    });

    ioc.run();
    EXPECT_TRUE(called);
}

TEST(HttpClientIntegrationTest, PostWithBody) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        EXPECT_EQ(req.method(), http::verb::post);
        EXPECT_EQ(req.body(), "hello");
        http::response<http::string_body> res;
        res.result(http::status::created);
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::HttpRequest req;
    req.method = "POST";
    req.target = "/api/data";
    req.body = "hello";
    req.host = "127.0.0.1";
    req.port = std::to_string(server.port());

    std::atomic<bool> called{false};
    client.async_request(req, [&](const cppload::net::HttpResponse& resp) {
        EXPECT_EQ(resp.status_code, 201);
        called = true;
    });

    ioc.run();
    EXPECT_TRUE(called);
}

TEST(HttpClientIntegrationTest, ServerError) {
    MockHttpServer server;
    server.set_handler([](const auto&) {
        http::response<http::string_body> res;
        res.result(http::status::internal_server_error);
        res.body() = "error";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::HttpRequest req;
    req.method = "GET";
    req.target = "/";
    req.host = "127.0.0.1";
    req.port = std::to_string(server.port());

    std::atomic<bool> called{false};
    client.async_request(req, [&](const cppload::net::HttpResponse& resp) {
        EXPECT_EQ(resp.status_code, 500);
        called = true;
    });

    ioc.run();
    EXPECT_TRUE(called);
}
