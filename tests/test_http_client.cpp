// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/net/http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <thread>
#include <atomic>

TEST(HttpClientTest, ConstructAndDestroy) {
    boost::asio::io_context ioc;
    ASSERT_NO_THROW({
        cppload::net::Http11Client client(ioc);
    });
}

TEST(HttpClientTest, SetTimeout) {
    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc);
    ASSERT_NO_THROW({
        client.set_timeout(std::chrono::milliseconds(1000));
    });
}

TEST(HttpClientTest, SetKeepAlive) {
    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc);
    ASSERT_NO_THROW({
        client.set_keep_alive(true);
    });
}

TEST(HttpClientTest, AsyncRequestFailsGracefully) {
    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc);
    client.set_timeout(std::chrono::milliseconds(100));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "192.0.2.1"; // Non-routable, will fail
    req.port = 99;

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        called = true;
        EXPECT_NE(ec.value(), 0);
    });

    ioc.run_for(std::chrono::seconds(5));
    EXPECT_TRUE(called);
}