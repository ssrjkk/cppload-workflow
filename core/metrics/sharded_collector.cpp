#include "cppload/metrics/sharded_collector.hpp"
#include <algorithm>
#include <thread>
#include <vector>
#include <numeric>

namespace cppload::metrics {

thread_local size_t ShardedMetricsCollector::t_shard_index = SIZE_MAX;
std::atomic<size_t> ShardedMetricsCollector::s_next_shard{0};

ShardedMetricsCollector::ShardedMetricsCollector()
    : s_num_shards_(std::min(kMaxShards,
        std::max<size_t>(4, std::thread::hardware_concurrency() * 2)))
    , shards_(std::make_unique<Shard[]>(s_num_shards_))
    , latency_buckets_(std::make_unique<LatencyBucket[]>(kNumLatencyBuckets))
    , start_time_(std::chrono::steady_clock::now())
{
}

ShardedMetricsCollector::~ShardedMetricsCollector() = default;

size_t ShardedMetricsCollector::get_shard_index() const {
    if (t_shard_index == SIZE_MAX) {
        t_shard_index = s_next_shard.fetch_add(1, std::memory_order_relaxed) % s_num_shards_;
    }
    return t_shard_index;
}

void ShardedMetricsCollector::record_request(uint16_t status_code,
                                              std::chrono::microseconds latency,
                                              size_t bytes_sent,
                                              size_t bytes_received) {
    auto idx = get_shard_index();
    auto& shard = shards_[idx];

    shard.requests.fetch_add(1, std::memory_order_relaxed);
    shard.bytes_sent.fetch_add(bytes_sent, std::memory_order_relaxed);
    shard.bytes_received.fetch_add(bytes_received, std::memory_order_relaxed);
    shard.latency_sum_us.fetch_add(latency.count(), std::memory_order_relaxed);

    if (status_code >= 200 && status_code < 400) {
        shard.successful.fetch_add(1, std::memory_order_relaxed);
    } else {
        shard.failed.fetch_add(1, std::memory_order_relaxed);
    }

    auto lat_val = static_cast<int64_t>(latency.count());

    auto min_curr = shard.min_latency_us.load(std::memory_order_relaxed);
    while (lat_val < min_curr &&
           !shard.min_latency_us.compare_exchange_weak(min_curr, lat_val,
               std::memory_order_relaxed)) {}

    auto max_curr = shard.max_latency_us.load(std::memory_order_relaxed);
    while (lat_val > max_curr &&
           !shard.max_latency_us.compare_exchange_weak(max_curr, lat_val,
               std::memory_order_relaxed)) {}

    if (lat_val < 0) lat_val = 0;
    if (lat_val >= kMaxLatencyUs) lat_val = kMaxLatencyUs - 1;
    auto bucket_idx = static_cast<size_t>(lat_val / kBucketWidthUs);
    latency_buckets_[bucket_idx].count.fetch_add(1, std::memory_order_relaxed);
}

ShardedMetrics ShardedMetricsCollector::snapshot() const {
    ShardedMetrics m;
    const auto ns = s_num_shards_.load(std::memory_order_relaxed);

    int64_t global_min = INT64_MAX;
    int64_t global_max = INT64_MIN;

    for (size_t i = 0; i < ns; ++i) {
        auto& shard = shards_[i];
        m.total_requests += shard.requests.load(std::memory_order_relaxed);
        m.successful_requests += shard.successful.load(std::memory_order_relaxed);
        m.failed_requests += shard.failed.load(std::memory_order_relaxed);
        m.total_bytes_sent += shard.bytes_sent.load(std::memory_order_relaxed);
        m.total_bytes_received += shard.bytes_received.load(std::memory_order_relaxed);

        int64_t s_min = shard.min_latency_us.load(std::memory_order_relaxed);
        int64_t s_max = shard.max_latency_us.load(std::memory_order_relaxed);
        if (s_min < global_min) global_min = s_min;
        if (s_max > global_max) global_max = s_max;
    }

    int64_t total_latency_sum = 0;
    for (size_t i = 0; i < ns; ++i) {
        total_latency_sum += shards_[i].latency_sum_us.load(std::memory_order_relaxed);
    }

    if (m.total_requests > 0) {
        m.mean_latency_us = static_cast<double>(total_latency_sum) / m.total_requests;
    }
    m.min_latency_us = (global_min == INT64_MAX) ? 0 : static_cast<uint64_t>(global_min);
    m.max_latency_us = (global_max == INT64_MIN) ? 0 : static_cast<uint64_t>(global_max);

    if (m.total_requests > 0) {
        uint64_t cumulative = 0;
        uint64_t p95_target = (m.total_requests * 95) / 100;
        uint64_t p99_target = (m.total_requests * 99) / 100;
        if (p95_target == 0) p95_target = 1;
        if (p99_target == 0) p99_target = 1;

        for (size_t b = 0; b < kNumLatencyBuckets; ++b) {
            cumulative += latency_buckets_[b].count.load(std::memory_order_relaxed);
            if (m.p95_latency_us == 0 && cumulative >= p95_target) {
                m.p95_latency_us = static_cast<uint64_t>(b * kBucketWidthUs);
            }
            if (m.p99_latency_us == 0 && cumulative >= p99_target) {
                m.p99_latency_us = static_cast<uint64_t>(b * kBucketWidthUs);
            }
        }
    }

    return m;
}

double ShardedMetricsCollector::requests_per_second() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double>(now - start_time_).count();
    if (elapsed < 0.001) return 0.0;

    uint64_t total = 0;
    const auto ns = s_num_shards_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < ns; ++i) {
        total += shards_[i].requests.load(std::memory_order_relaxed);
    }
    return static_cast<double>(total) / elapsed;
}

double ShardedMetricsCollector::error_rate() const {
    uint64_t total = 0;
    uint64_t failed = 0;
    const auto ns = s_num_shards_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < ns; ++i) {
        total += shards_[i].requests.load(std::memory_order_relaxed);
        failed += shards_[i].failed.load(std::memory_order_relaxed);
    }
    if (total == 0) return 0.0;
    return static_cast<double>(failed) / total * 100.0;
}

void ShardedMetricsCollector::reset() {
    const auto ns = s_num_shards_.load(std::memory_order_relaxed);
    for (size_t i = 0; i < ns; ++i) {
        shards_[i].requests.store(0, std::memory_order_relaxed);
        shards_[i].successful.store(0, std::memory_order_relaxed);
        shards_[i].failed.store(0, std::memory_order_relaxed);
        shards_[i].bytes_sent.store(0, std::memory_order_relaxed);
        shards_[i].bytes_received.store(0, std::memory_order_relaxed);
        shards_[i].latency_sum_us.store(0, std::memory_order_relaxed);
        shards_[i].min_latency_us.store(INT64_MAX, std::memory_order_relaxed);
        shards_[i].max_latency_us.store(INT64_MIN, std::memory_order_relaxed);
    }
    for (size_t b = 0; b < kNumLatencyBuckets; ++b) {
        latency_buckets_[b].count.store(0, std::memory_order_relaxed);
    }
    start_time_ = std::chrono::steady_clock::now();
}

} // namespace cppload::metrics
