#include <gtest/gtest.h>
#include "cppload/net/http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <thread>

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
