#pragma once

#include <chrono>
#include <mutex>

namespace cppload {

class TokenBucket {
public:
    explicit TokenBucket(double rate, double burst = 0);
    void set_rate(double rate);
    void set_burst(double burst);
    void consume();
    bool try_consume();

private:
    void refill();
    std::mutex mutex_;
    double rate_;
    double burst_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
};

} // namespace cppload
