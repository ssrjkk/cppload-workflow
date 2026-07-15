#include "cppload/core/token_bucket.hpp"
#include <condition_variable>
#include <thread>

namespace cppload {

TokenBucket::TokenBucket(double rate, double burst)
    : rate_(rate)
    , burst_(burst > 0 ? burst : rate)
    , tokens_(burst_)
    , last_refill_(std::chrono::steady_clock::now())
{
    if (rate_ <= 0.0) throw std::invalid_argument("TokenBucket: rate must be > 0");
}

void TokenBucket::set_rate(double rate) {
    if (rate <= 0.0) throw std::invalid_argument("TokenBucket: rate must be > 0");
    std::lock_guard<std::mutex> lock(mtx_);
    refill();
    rate_ = rate;
    cv_.notify_one();
}

void TokenBucket::set_burst(double burst) {
    if (burst <= 0.0) throw std::invalid_argument("TokenBucket: burst must be > 0");
    std::lock_guard<std::mutex> lock(mtx_);
    refill();
    burst_ = burst;
    if (tokens_ > burst_) tokens_ = burst_;
    cv_.notify_one();
}

void TokenBucket::consume() {
    std::unique_lock<std::mutex> lock(mtx_);
    refill();
    while (tokens_ < 1.0) {
        double deficit = 1.0 - tokens_;
        double wait_sec = deficit / rate_;
        if (wait_sec > 1.0) wait_sec = 1.0;
        auto wait_ns = std::chrono::nanoseconds(
            static_cast<int64_t>(wait_sec * 1'000'000'000));
        if (wait_ns.count() > 0) {
            cv_.wait_for(lock, wait_ns, [this] { return tokens_ >= 1.0; });
        }
        refill();
    }
    tokens_ -= 1.0;
}

bool TokenBucket::try_consume() {
    std::lock_guard<std::mutex> lock(mtx_);
    refill();
    if (tokens_ < 1.0) {
        return false;
    }
    tokens_ -= 1.0;
    return true;
}

void TokenBucket::refill() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
    if (elapsed > 0) {
        tokens_ += elapsed * rate_;
        if (tokens_ > burst_) tokens_ = burst_;
        last_refill_ = now;
    }
}

} // namespace cppload
