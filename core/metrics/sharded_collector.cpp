// @author ssrjkk | cppload
#include "cppload/metrics/sharded_collector.hpp"
#include "cppload/core/constants.hpp"
#include <algorithm>
#include <thread>
#include <vector>
#include <numeric>
#include <climits>

namespace cppload::metrics {

thread_local size_t ShardedMetricsCollector::t_shard_index = SIZE_MAX;
std::atomic<size_t> ShardedMetricsCollector::s_next_shard{0};

ShardedMetricsCollector::ShardedMetricsCollector()
    : num_shards_(std::min(kMaxShards,
        std::max<size_t>(core::kMinShards, std::thread::hardware_concurrency() * 2)))
    , shards_(std::make_unique<Shard[]>(num_shards_))
    , latency_samples_(kMaxLatencySamples)
    , rps_window_start_(std::chrono::steady_clock::now())
{
}

ShardedMetricsCollector::~ShardedMetricsCollector() = default;

size_t ShardedMetricsCollector::get_shard_index() const {
    if (t_shard_index == SIZE_MAX) {
        t_shard_index = s_next_shard.fetch_add(1, std::memory_order_relaxed) % num_shards_;
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
    auto lat_count = latency.count();
    if (lat_count <= 0) {
        // Ignore negative/zero latency for sum to prevent underflow
    } else {
        auto cur_sum = shard.latency_sum_us.load(std::memory_order_relaxed);
        while (true) {
            if (cur_sum > UINT64_MAX - static_cast<uint64_t>(lat_count)) {
                shard.latency_sum_us.store(UINT64_MAX, std::memory_order_relaxed);
                break;
            }
            if (shard.latency_sum_us.compare_exchange_weak(
                    cur_sum, cur_sum + static_cast<uint64_t>(lat_count), std::memory_order_relaxed))
                break;
        }
    }

    if (status_code >= 200 && status_code < 400) {
        shard.successful.fetch_add(1, std::memory_order_relaxed);
    } else {
        shard.failed.fetch_add(1, std::memory_order_relaxed);
    }

    auto lat_val = static_cast<int64_t>(latency.count());
    if (lat_val < 0) lat_val = 0;

    auto min_curr = shard.min_latency_us.load(std::memory_order_relaxed);
    while (lat_val < min_curr &&
           !shard.min_latency_us.compare_exchange_weak(min_curr, lat_val,
               std::memory_order_relaxed)) {}

    auto max_curr = shard.max_latency_us.load(std::memory_order_relaxed);
    while (lat_val > max_curr &&
           !shard.max_latency_us.compare_exchange_weak(max_curr, lat_val,
               std::memory_order_relaxed)) {}

    {
        std::lock_guard<std::mutex> lock(samples_mtx_);
        latency_samples_[samples_head_] = static_cast<uint64_t>(lat_val);
        samples_head_ = (samples_head_ + 1) % kMaxLatencySamples;
        if (samples_count_ < kMaxLatencySamples) ++samples_count_;
    }

    rps_count_.fetch_add(1, std::memory_order_relaxed);
}

ShardedMetrics ShardedMetricsCollector::snapshot() const {
    ShardedMetrics m;
    const auto ns = num_shards_;

    int64_t global_min = INT64_MAX;
    int64_t global_max = INT64_MIN;
    int64_t total_latency_sum = 0;

    for (size_t i = 0; i < ns; ++i) {
        auto& shard = shards_[i];
        m.total_requests += shard.requests.load(std::memory_order_relaxed);
        m.successful_requests += shard.successful.load(std::memory_order_relaxed);
        m.failed_requests += shard.failed.load(std::memory_order_relaxed);
        m.total_bytes_sent += shard.bytes_sent.load(std::memory_order_relaxed);
        m.total_bytes_received += shard.bytes_received.load(std::memory_order_relaxed);
        total_latency_sum += shard.latency_sum_us.load(std::memory_order_relaxed);

        int64_t s_min = shard.min_latency_us.load(std::memory_order_relaxed);
        int64_t s_max = shard.max_latency_us.load(std::memory_order_relaxed);
        if (s_min < global_min) global_min = s_min;
        if (s_max > global_max) global_max = s_max;
    }

    if (m.total_requests > 0) {
        m.mean_latency_us = static_cast<double>(total_latency_sum) / m.total_requests;
    }
    m.min_latency_us = (global_min == INT64_MAX) ? 0 : static_cast<uint64_t>(global_min);
    m.max_latency_us = (global_max == INT64_MIN) ? 0 : static_cast<uint64_t>(global_max);

    if (m.total_requests > 0) {
        std::vector<uint64_t> sorted;
        {
            std::lock_guard<std::mutex> lock(samples_mtx_);
            sorted.reserve(samples_count_);
            if (samples_count_ == kMaxLatencySamples) {
                // Ring is full: oldest samples live after the head.
                sorted.insert(sorted.end(),
                    latency_samples_.begin() + samples_head_,
                    latency_samples_.end());
                sorted.insert(sorted.end(),
                    latency_samples_.begin(),
                    latency_samples_.begin() + samples_head_);
            } else {
                sorted.assign(latency_samples_.begin(),
                    latency_samples_.begin() + samples_count_);
            }
        }
        std::sort(sorted.begin(), sorted.end());

        const size_t n = sorted.size();
        if (n > 0) {
            size_t idx95 = (n * 95 + 99) / 100 - 1;
            size_t idx99 = (n * 99 + 99) / 100 - 1;
            m.p95_latency_us = sorted[idx95];
            m.p99_latency_us = sorted[idx99];
        }
    }

    return m;
}

double ShardedMetricsCollector::requests_per_second() const {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rps_mtx_);
    auto elapsed = std::chrono::duration<double>(now - rps_window_start_).count();

    if (elapsed <= 0.0) return 0.0;

    // Once at least one second has elapsed, report the rate over that whole
    // window and start a fresh one so idle time never keeps the average alive.
    if (elapsed >= 1.0) {
        uint64_t count = rps_count_.exchange(0, std::memory_order_relaxed);
        double rps = static_cast<double>(count) / elapsed;
        rps_window_start_ = now;
        return rps;
    }

    return static_cast<double>(rps_count_.load(std::memory_order_relaxed)) / elapsed;
}

double ShardedMetricsCollector::error_rate() const {
    uint64_t total = 0;
    uint64_t failed = 0;
    const auto ns = num_shards_;
    for (size_t i = 0; i < ns; ++i) {
        total += shards_[i].requests.load(std::memory_order_relaxed);
        failed += shards_[i].failed.load(std::memory_order_relaxed);
    }
    if (total == 0) return 0.0;
    return static_cast<double>(failed) / total * 100.0;
}

void ShardedMetricsCollector::reset() {
    const auto ns = num_shards_;
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
    {
        std::lock_guard<std::mutex> lock(samples_mtx_);
        samples_head_ = 0;
        samples_count_ = 0;
    }
    {
        std::lock_guard<std::mutex> lock(rps_mtx_);
        rps_count_.store(0, std::memory_order_relaxed);
        rps_window_start_ = std::chrono::steady_clock::now();
    }
}

} // namespace cppload::metrics