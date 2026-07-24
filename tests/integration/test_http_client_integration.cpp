// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/net/http_client.hpp"
#include "mock_server.hpp"
#include <boost/asio/io_context.hpp>
#include <atomic>
#include <thread>

TEST(HttpClientIntegrationTest, SuccessfulRequest) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target() != "/api/test" || req.method() != http::verb::get) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = R"({"status":"ok"})";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/api/test";
    req.host = "127.0.0.1";
    req.port = server.port();

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 200);
        EXPECT_EQ(resp.body, R"({"status":"ok"})");
        EXPECT_GT(resp.latency.count(), 0);
        called = true;
    });

    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST(HttpClientIntegrationTest, PostWithBody) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target() != "/api/data" || req.method() != http::verb::post) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }
        res.result(http::status::created);
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "POST";
    req.path = "/api/data";
    req.body = "hello";
    req.host = "127.0.0.1";
    req.port = server.port();

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 201);
        called = true;
    });

    ioc.run_for(std::chrono::seconds(10));
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
    cppload::net::Http11Client client(ioc);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "127.0.0.1";
    req.port = server.port();

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 500);
        called = true;
    });

    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}