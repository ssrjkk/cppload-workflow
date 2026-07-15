#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace cppload {

class TokenBucket {
public:
    explicit TokenBucket(double rate, double burst = 0);
    TokenBucket(const TokenBucket&) = delete;
    TokenBucket& operator=(const TokenBucket&) = delete;
    TokenBucket(TokenBucket&&) = delete;
    TokenBucket& operator=(TokenBucket&&) = delete;
    void set_rate(double rate);
    void set_burst(double burst);
    void consume();
    [[nodiscard]] bool try_consume();

private:
    void refill();
    std::mutex mtx_;
    std::condition_variable cv_;
    double rate_;
    double burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

} // namespace cppload
