#include "cppload/net/connection_pool.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <chrono>

namespace cppload::net {

class ConnectionPool::Impl {
public:
    Impl(boost::asio::io_context& ioc, const PoolConfig& config)
        : ioc_(ioc), config_(config), total_created_(0) {}
    
    std::unique_ptr<HttpClient> acquire(const std::string& host,
                                        const std::string& port) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto key = host + ":" + port;
        auto& pool = pools_[key];
        
        // Try to get from pool
        if (!pool.empty()) {
            auto client = std::move(pool.front());
            pool.pop();
            return client;
        }
        
        // Create new connection if under limit
        if (total_created_ < config_.max_connections) {
            total_created_++;
            return std::make_unique<HttpClient>(ioc_);
        }
        
        // Pool exhausted - return nullptr (caller should retry or fail)
        return nullptr;
    }
    
    void release(std::unique_ptr<HttpClient> client,
                 const std::string& host,
                 const std::string& port) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto key = host + ":" + port;
        auto& pool = pools_[key];
        
        if (pool.size() < config_.max_connections) {
            client->set_keep_alive(config_.keep_alive);
            pool.push(std::move(client));
        }
        // If pool is full, let client be destroyed
    }
    
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        // In real implementation, check idle timeout and close old connections
        // For MVP, just log
    }
    
    Stats stats() const {
        std::lock_guard<std::mutex> lock(mutex_);
        Stats s;
        s.total_created = total_created_;
        
        for (const auto& [key, pool] : pools_) {
            s.idle_connections += pool.size();
        }
        
        s.active_connections = total_created_ - s.idle_connections;
        return s;
    }
    
private:
    boost::asio::io_context& ioc_;
    PoolConfig config_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::queue<std::unique_ptr<HttpClient>>> pools_;
    size_t total_created_;
};

ConnectionPool::ConnectionPool(boost::asio::io_context& ioc, 
                               const PoolConfig& config)
    : impl_(std::make_unique<Impl>(ioc, config)) {}

ConnectionPool::~ConnectionPool() = default;

std::unique_ptr<HttpClient> ConnectionPool::acquire(const std::string& host,
                                                    const std::string& port) {
    return impl_->acquire(host, port);
}

void ConnectionPool::release(std::unique_ptr<HttpClient> client,
                            const std::string& host,
                            const std::string& port) {
    impl_->release(std::move(client), host, port);
}

void ConnectionPool::cleanup() { impl_->cleanup(); }

ConnectionPool::Stats ConnectionPool::stats() const { return impl_->stats(); }

} // namespace cppload::net
