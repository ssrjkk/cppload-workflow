#include "cppload/net/connection_pool.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <atomic>
#include <chrono>
#include <thread>

namespace cppload::net {

struct PoolEntry {
    std::unique_ptr<Http11Client> client;
    std::chrono::steady_clock::time_point idle_since;
};

class ConnectionPool::Impl {
public:
    Impl(boost::asio::io_context& ioc, const PoolConfig& config)
        : ioc_(ioc), config_(config), total_created_(0) {
        auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < config_.min_connections; ++i) {
            total_created_++;
            auto client = std::make_unique<Http11Client>(ioc_);
            pools_["__warmup__"].push({std::move(client), now});
        }
        pools_.erase("__warmup__");
    }

    std::unique_ptr<Http11Client> acquire(const std::string& host,
                                          uint16_t port) {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        auto key = host + ":" + std::to_string(port);
        auto& pool = pools_[key];

        if (!pool.empty()) {
            auto entry = std::move(pool.front());
            pool.pop();
            lock_.clear(std::memory_order_release);
            return std::move(entry.client);
        }

        if (total_created_ < config_.max_connections) {
            total_created_++;
            lock_.clear(std::memory_order_release);
            return std::make_unique<Http11Client>(ioc_);
        }

        lock_.clear(std::memory_order_release);
        return nullptr;
    }

    void release(std::unique_ptr<Http11Client> client,
                 const std::string& host,
                 uint16_t port) {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        auto key = host + ":" + std::to_string(port);
        auto& pool = pools_[key];

        if (pool.size() < config_.max_connections) {
            client->set_keep_alive(config_.keep_alive);
            auto now = std::chrono::steady_clock::now();
            pool.push({std::move(client), now});
        }
        lock_.clear(std::memory_order_release);
    }

    void cleanup() {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        auto now = std::chrono::steady_clock::now();
        auto idle_timeout = config_.idle_timeout;
        for (auto it = pools_.begin(); it != pools_.end(); ) {
            auto& q = it->second;
            // Remove expired connections (oldest at front)
            while (!q.empty()) {
                auto& entry = q.front();
                auto age = std::chrono::duration_cast<std::chrono::seconds>(
                    now - entry.idle_since);
                if (age >= idle_timeout) {
                    q.pop();
                } else {
                    break;
                }
            }
            if (q.empty()) {
                it = pools_.erase(it);
            } else {
                ++it;
            }
        }
        lock_.clear(std::memory_order_release);
    }

    Stats stats() const {
        while (lock_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        Stats s;
        s.total_created = total_created_;

        for (const auto& [key, pool] : pools_) {
            s.idle_connections += pool.size();
        }

        s.active_connections = total_created_ - s.idle_connections;
        lock_.clear(std::memory_order_release);
        return s;
    }

private:
    boost::asio::io_context& ioc_;
    PoolConfig config_;
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::unordered_map<std::string, std::queue<PoolEntry>> pools_;
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
