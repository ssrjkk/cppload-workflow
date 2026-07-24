// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/net/connection_pool.hpp"
#include <boost/asio/io_context.hpp>
#include <thread>

using namespace cppload::net;

TEST(ConnectionPoolTest, AcquireAndRelease) {
    boost::asio::io_context ioc;
    PoolConfig cfg;
    cfg.max_connections = 5;
    ConnectionPool pool(ioc, cfg);

    auto client = pool.acquire("example.com", 80);
    ASSERT_NE(client, nullptr);

    auto stats = pool.stats();
    EXPECT_EQ(stats.total_created, 1u);
    EXPECT_EQ(stats.active_connections, 1u);

    pool.release(std::move(client), "example.com", 80);

    stats = pool.stats();
    EXPECT_EQ(stats.total_created, 1u);
    EXPECT_EQ(stats.idle_connections, 1u);
    EXPECT_EQ(stats.active_connections, 0u);
}

TEST(ConnectionPoolTest, ReusesIdleConnection) {
    boost::asio::io_context ioc;
    PoolConfig cfg;
    cfg.max_connections = 5;
    ConnectionPool pool(ioc, cfg);

    auto c1 = pool.acquire("example.com", 80);
    pool.release(std::move(c1), "example.com", 80);

    auto c2 = pool.acquire("example.com", 80);
    ASSERT_NE(c2, nullptr);

    auto stats = pool.stats();
    EXPECT_EQ(stats.total_created, 1u);
    EXPECT_EQ(stats.idle_connections, 0u);

    pool.release(std::move(c2), "example.com", 80);
}

TEST(ConnectionPoolTest, RespectsMaxConnections) {
    boost::asio::io_context ioc;
    PoolConfig cfg;
    cfg.max_connections = 2;
    ConnectionPool pool(ioc, cfg);

    auto c1 = pool.acquire("example.com", 80);
    auto c2 = pool.acquire("example.com", 80);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);

    auto c3 = pool.acquire("example.com", 80);
    EXPECT_EQ(c3, nullptr);

    pool.release(std::move(c1), "example.com", 80);
    pool.release(std::move(c2), "example.com", 80);
}

TEST(ConnectionPoolTest, CleanupRemovesIdleConnections) {
    boost::asio::io_context ioc;
    PoolConfig cfg;
    cfg.max_connections = 10;
    cfg.idle_timeout = std::chrono::seconds(0);
    ConnectionPool pool(ioc, cfg);

    auto c1 = pool.acquire("example.com", 80);
    pool.release(std::move(c1), "example.com", 80);

    auto stats = pool.stats();
    EXPECT_EQ(stats.idle_connections, 1u);

    pool.cleanup();

    stats = pool.stats();
    EXPECT_EQ(stats.idle_connections, 0u);
    EXPECT_EQ(stats.total_created, 0u);
}

TEST(ConnectionPoolTest, DifferentHostsSeparatePools) {
    boost::asio::io_context ioc;
    PoolConfig cfg;
    cfg.max_connections = 10;
    ConnectionPool pool(ioc, cfg);

    auto c1 = pool.acquire("host1.com", 80);
    auto c2 = pool.acquire("host2.com", 80);
    pool.release(std::move(c1), "host1.com", 80);
    pool.release(std::move(c2), "host2.com", 80);

    auto stats = pool.stats();
    EXPECT_EQ(stats.total_created, 2u);
    EXPECT_EQ(stats.idle_connections, 2u);
}

TEST(ConnectionPoolTest, ConcurrentAcquireRelease) {
    boost::asio::io_context ioc;
    PoolConfig cfg;
    cfg.max_connections = 50;
    ConnectionPool pool(ioc, cfg);

    std::vector<std::thread> threads;
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&pool]() {
            for (int i = 0; i < 100; ++i) {
                auto c = pool.acquire("example.com", 80);
                if (c) {
                    std::this_thread::yield();
                    pool.release(std::move(c), "example.com", 80);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    auto stats = pool.stats();
    EXPECT_LE(stats.total_created, 50u);
    EXPECT_EQ(stats.active_connections, 0u);
}