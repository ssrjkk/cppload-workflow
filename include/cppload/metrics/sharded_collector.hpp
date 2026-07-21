#pragma once

#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>

namespace cppload::metrics {

struct ShardedMetrics {
    uint64_t total_requests{0};
    uint64_t successful_requests{0};
    uint64_t failed_requests{0};
    uint64_t total_bytes_sent{0};
    uint64_t total_bytes_received{0};
    double mean_latency_us{0.0};
    uint64_t min_latency_us{0};
    uint64_t max_latency_us{0};
    uint64_t p95_latency_us{0};
    uint64_t p99_latency_us{0};
};

class ShardedMetricsCollector {
public:
    ShardedMetricsCollector();
    ~ShardedMetricsCollector();

    ShardedMetricsCollector(const ShardedMetricsCollector&) = delete;
    ShardedMetricsCollector& operator=(const ShardedMetricsCollector&) = delete;

    void record_request(uint16_t status_code,
                       std::chrono::microseconds latency,
                       size_t bytes_sent,
                       size_t bytes_received);

    [[nodiscard]] ShardedMetrics snapshot() const;

    [[nodiscard]] double requests_per_second() const;
    [[nodiscard]] double error_rate() const;

    void reset();

    static constexpr size_t kMaxShards = 128;

private:
    struct alignas(64) Shard {
        std::atomic<uint64_t> requests{0};
        std::atomic<uint64_t> successful{0};
        std::atomic<uint64_t> failed{0};
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<int64_t> latency_sum_us{0};
        std::atomic<int64_t> min_latency_us{INT64_MAX};
        std::atomic<int64_t> max_latency_us{INT64_MIN};
    };

    struct alignas(64) LatencyBucket {
        std::atomic<uint64_t> count{0};
        char padding[64 - sizeof(std::atomic<uint64_t>)]{};
    };

    static constexpr size_t kNumLatencyBuckets = 256;
    static constexpr int64_t kMaxLatencyUs = 30000000LL;
    static constexpr int64_t kBucketWidthUs = kMaxLatencyUs / kNumLatencyBuckets;

    static thread_local size_t t_shard_index;
    static std::atomic<size_t> s_next_shard;

    size_t get_shard_index() const;
    size_t num_shards() const { return s_num_shards_.load(std::memory_order_relaxed); }

    size_t s_num_shards_;
    std::unique_ptr<Shard[]> shards_;
    std::unique_ptr<LatencyBucket[]> latency_buckets_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace cppload::metrics
