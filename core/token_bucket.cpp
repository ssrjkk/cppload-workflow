#include "cppload/core/token_bucket.hpp"
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
    while (lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    refill();
    rate_ = rate;
    lock_.clear(std::memory_order_release);
}

void TokenBucket::set_burst(double burst) {
    if (burst <= 0.0) throw std::invalid_argument("TokenBucket: burst must be > 0");
    while (lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    refill();
    burst_ = burst;
    if (tokens_ > burst_) tokens_ = burst_;
    lock_.clear(std::memory_order_release);
}

void TokenBucket::consume() {
    while (lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    refill();
    while (tokens_ < 1.0) {
        double deficit = 1.0 - tokens_;
        double wait_sec = deficit / rate_;
        if (wait_sec > 1.0) wait_sec = 1.0;
        auto wait_us = std::chrono::microseconds(
            static_cast<int64_t>(wait_sec * 1'000'000));
        lock_.clear(std::memory_order_release);
        if (wait_us.count() > 0) {
            std::this_thread::sleep_for(wait_us);
        } else {
            std::this_thread::yield();
        }
        while (lock_.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        refill();
    }
    tokens_ -= 1.0;
    lock_.clear(std::memory_order_release);
}

bool TokenBucket::try_consume() {
    while (lock_.test_and_set(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    refill();
    if (tokens_ < 1.0) {
        lock_.clear(std::memory_order_release);
        return false;
    }
    tokens_ -= 1.0;
    lock_.clear(std::memory_order_release);
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
