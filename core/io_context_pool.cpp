#include "cppload/core/io_context_pool.hpp"

namespace cppload {

IoContextPool::IoContextPool(std::size_t pool_size) {
    if (pool_size == 0) {
        pool_size = std::thread::hardware_concurrency();
        if (pool_size == 0) pool_size = 2;
    }
    contexts_.reserve(pool_size);
    work_guards_.reserve(pool_size);
    for (std::size_t i = 0; i < pool_size; ++i) {
        auto ctx = std::make_unique<IoContext>();
        work_guards_.emplace_back(
            boost::asio::make_work_guard(*ctx));
        contexts_.push_back(std::move(ctx));
    }
}

IoContextPool::~IoContextPool() {
    stop();
}

void IoContextPool::start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true))
        return;
    threads_.reserve(contexts_.size());
    for (auto& ctx : contexts_) {
        threads_.emplace_back([ctx = ctx.get()]() {
            ctx->run();
        });
    }
}

void IoContextPool::stop() {
    work_guards_.clear();
    for (auto& ctx : contexts_) {
        ctx->stop();
    }
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
    started_ = false;
}

boost::asio::io_context& IoContextPool::get_context() {
    auto idx = next_.fetch_add(1, std::memory_order_relaxed) % contexts_.size();
    return *contexts_[idx];
}

} // namespace cppload
