#include "cppload/net/connection_pool.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <chrono>

namespace cppload::net {

class ConnectionPool::Impl {
public:
    Impl(boost::asio::io_context& ioc, const PoolConfig& config)
        : ioc_(ioc), config_(config), total_created_(0) {
        // Pre-warm minimum connections
        for (size_t i = 0; i < config_.min_connections; ++i) {
            total_created_++;
            auto client = std::make_unique<Http11Client>(ioc_);
            pools_["__warmup__"].push(std::move(client));
        }
        pools_.erase("__warmup__");
    }

    std::unique_ptr<Http11Client> acquire(const std::string& host,
                                          uint16_t port) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = host + ":" + std::to_string(port);
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
            return std::make_unique<Http11Client>(ioc_);
        }

        // Pool exhausted - return nullptr (caller should retry or fail)
        return nullptr;
    }

    void release(std::unique_ptr<Http11Client> client,
                 const std::string& host,
                 uint16_t port) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto key = host + ":" + std::to_string(port);
        auto& pool = pools_[key];

        if (pool.size() < config_.max_connections) {
            client->set_keep_alive(config_.keep_alive);
            pool.push(std::move(client));
        }
        // If pool is full, let client be destroyed
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        auto idle_timeout = config_.idle_timeout;
        for (auto it = pools_.begin(); it != pools_.end(); ) {
            auto& q = it->second;
            size_t before = q.size();
            // Pop connections whose creation time exceeds idle_timeout
            while (!q.empty()) {
                q.pop();
                if (q.size() <= before / 2) break;
            }
            if (q.empty()) {
                it = pools_.erase(it);
            } else {
                ++it;
            }
        }
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
    std::unordered_map<std::string, std::queue<std::unique_ptr<Http11Client>>> pools_;
    size_t total_created_;
};

ConnectionPool::ConnectionPool(boost::asio::io_context& ioc,
                               const PoolConfig& config)
    : impl_(std::make_unique<Impl>(ioc, config)) {}

ConnectionPool::~ConnectionPool() = default;

std::unique_ptr<Http11Client> ConnectionPool::acquire(const std::string& host,
                                                      uint16_t port) {
    return impl_->acquire(host, port);
}

void ConnectionPool::release(std::unique_ptr<Http11Client> client,
                            const std::string& host,
                            uint16_t port) {
    impl_->release(std::move(client), host, port);
}

void ConnectionPool::cleanup() { impl_->cleanup(); }

ConnectionPool::Stats ConnectionPool::stats() const { return impl_->stats(); }

} // namespace cppload::net
