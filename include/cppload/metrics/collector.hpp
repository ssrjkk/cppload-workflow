#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cppload::metrics {

struct RequestMetrics {
    uint64_t total_requests{0};
    uint64_t successful_requests{0};
    uint64_t failed_requests{0};
    uint64_t total_bytes_sent{0};
    uint64_t total_bytes_received{0};

    std::chrono::microseconds min_latency{std::chrono::microseconds::max()};
    std::chrono::microseconds max_latency{std::chrono::microseconds::min()};
    double mean_latency_us{0.0};
    uint64_t p95_latency_us{0};
    uint64_t p99_latency_us{0};
};

class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();

    void record_request(uint16_t status_code,
                       std::chrono::microseconds latency,
                       size_t bytes_sent,
                       size_t bytes_received);

    RequestMetrics snapshot() const;

    double requests_per_second() const;
    double error_rate() const;
    uint64_t p95_latency_us() const;
    uint64_t p99_latency_us() const;

    void reset();

private:
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::atomic<uint64_t> total_bytes_sent_{0};
    std::atomic<uint64_t> total_bytes_received_{0};

    std::atomic<uint64_t> cumulative_latency_us_{0};
    std::atomic<std::chrono::microseconds::rep> min_latency_us_{
        std::chrono::microseconds::max().count()};
    std::atomic<std::chrono::microseconds::rep> max_latency_us_{
        std::chrono::microseconds::min().count()};

    std::atomic<std::chrono::steady_clock::time_point::rep> start_time_{
        std::chrono::steady_clock::now().time_since_epoch().count()};

    struct Cell {
        std::atomic<uint64_t> seq{0};
        int64_t value{0};
    };

    static constexpr size_t kRingCapacity = 1 << 20;
    static constexpr size_t kRingMask = kRingCapacity - 1;

    std::unique_ptr<Cell[]> ring_;
    mutable std::atomic<uint64_t> head_{0};
    mutable std::atomic<uint64_t> tail_{0};
};

} // namespace cppload::metrics
