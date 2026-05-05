#pragma once

#include "http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>
#include <queue>
#include <mutex>
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
    std::unique_ptr<HttpClient> acquire(const std::string& host, 
                                        const std::string& port);
    
    // Return connection to pool
    void release(std::unique_ptr<HttpClient> client,
                 const std::string& host,
                 const std::string& port);
    
    // Cleanup idle connections
    void cleanup();
    
    // Get pool statistics
    struct Stats {
        size_t active_connections{0};
        size_t idle_connections{0};
        size_t total_created{0};
    };
    
    Stats stats() const;
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cppload::net
