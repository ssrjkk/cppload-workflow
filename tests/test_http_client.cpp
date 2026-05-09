#include <gtest/gtest.h>
#include "cppload/net/http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <thread>
#include <atomic>

TEST(HttpClientTest, ConstructAndDestroy) {
    boost::asio::io_context ioc;
    ASSERT_NO_THROW({
        cppload::net::HttpClient client(ioc);
    });
}

TEST(HttpClientTest, SetTimeout) {
    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    ASSERT_NO_THROW({
        client.set_timeout(std::chrono::milliseconds(1000));
    });
}

TEST(HttpClientTest, SetKeepAlive) {
    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    ASSERT_NO_THROW({
        client.set_keep_alive(true);
    });
}

TEST(HttpClientTest, AsyncRequestFailsGracefully) {
    boost::asio::io_context ioc;
    cppload::net::HttpClient client(ioc);
    client.set_timeout(std::chrono::milliseconds(100));

    cppload::net::HttpRequest req;
    req.method = "GET";
    req.target = "/";
    req.host = "192.0.2.1"; // Non-routable, will fail
    req.port = "99";

    std::atomic<bool> called{false};
    client.async_request(req, [&](const cppload::net::HttpResponse& resp) {
        called = true;
        EXPECT_EQ(resp.status_code, 0);
    });

    ioc.run();
    EXPECT_TRUE(called);
}
