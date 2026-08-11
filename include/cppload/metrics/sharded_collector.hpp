// @author ssrjkk | cppload
#pragma once

#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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
    ShardedMetricsCollector(ShardedMetricsCollector&&) = delete;
    ShardedMetricsCollector& operator=(ShardedMetricsCollector&&) = delete;

    void record_request(uint16_t status_code,
                       std::chrono::microseconds latency,
                       size_t bytes_sent,
                       size_t bytes_received);

    [[nodiscard]] ShardedMetrics snapshot() const;

    [[nodiscard]] double requests_per_second() const;
    [[nodiscard]] double error_rate() const;

    void reset();

    static constexpr size_t kMaxShards = 128;

    // Upper bound on retained latency samples for p95/p99 estimation.
    static constexpr size_t kMaxLatencySamples = 8192;

private:
    struct alignas(64) Shard {
        std::atomic<uint64_t> requests{0};
        std::atomic<uint64_t> successful{0};
        std::atomic<uint64_t> failed{0};
        std::atomic<uint64_t> bytes_sent{0};
        std::atomic<uint64_t> bytes_received{0};
        std::atomic<uint64_t> latency_sum_us{0};
        std::atomic<int64_t> min_latency_us{INT64_MAX};
        std::atomic<int64_t> max_latency_us{INT64_MIN};
    };

    static thread_local size_t t_shard_index;
    static std::atomic<size_t> s_next_shard;

    size_t get_shard_index() const;
    size_t num_shards() const { return num_shards_; }

    const size_t num_shards_;
    std::unique_ptr<Shard[]> shards_;

    // Bounded ring buffer of raw latency samples used for exact percentile
    // estimation at snapshot() time.
    mutable std::mutex samples_mtx_;
    std::vector<uint64_t> latency_samples_;
    size_t samples_head_{0};
    size_t samples_count_{0};

    // Windowed request rate: counter reset every second so a burst followed
    // by idle reports a fresh ~0 instead of an ever-draining average.
    mutable std::mutex rps_mtx_;
    mutable std::atomic<uint64_t> rps_count_{0};
    mutable std::chrono::steady_clock::time_point rps_window_start_;
};

} // namespace cppload::metrics