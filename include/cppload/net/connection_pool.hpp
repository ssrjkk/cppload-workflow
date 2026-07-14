#pragma once

#include "cppload/net/http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <queue>
#include <unordered_map>

namespace cppload::net {

struct PoolConfig {
    size_t min_connections{5};
    size_t max_connections{100};
    std::chrono::seconds idle_timeout{30};
    bool keep_alive{true};
};

class ConnectionPool {
public:
    explicit ConnectionPool(boost::asio::io_context& ioc,
                           const PoolConfig& config = {});
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    // Get a connection from pool (creates new if needed)
    std::unique_ptr<Http11Client> acquire(const std::string& host,
                                          uint16_t port);

    // Return connection to pool
    void release(std::unique_ptr<Http11Client> client,
                 const std::string& host,
                 uint16_t port);

    // Cleanup idle connections
    void cleanup();

    // Get pool statistics
    struct Stats {
        size_t active_connections{0};
        size_t idle_connections{0};
        size_t total_created{0};
    };

    [[nodiscard]] Stats stats() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::net
