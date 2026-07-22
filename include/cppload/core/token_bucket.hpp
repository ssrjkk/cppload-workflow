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

    template <typename Rep, typename Period>
    [[nodiscard]] bool try_consume_for(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock<std::mutex> lock(mtx_);
        refill();
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        double deficit = 1.0 - tokens_;
        double wait_sec = deficit / rate_;
        auto max_wait = std::chrono::duration<double>(timeout);
        if (wait_sec > max_wait.count()) return false;
        auto wait_ns = std::chrono::nanoseconds(
            static_cast<int64_t>(wait_sec * 1'000'000'000));
        if (wait_ns.count() > 0) {
            cv_.wait_for(lock, wait_ns, [this] { return tokens_ >= 1.0; });
        }
        refill();
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return true;
        }
        return false;
    }

    [[nodiscard]] double tokens_available() const;

private:
    void refill();
    mutable std::mutex mtx_;
    mutable std::condition_variable cv_;
    double rate_;
    double burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

} // namespace cppload
