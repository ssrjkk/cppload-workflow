#include "cppload/metrics/collector.hpp"
#include <algorithm>

namespace cppload::metrics {

MetricsCollector::MetricsCollector()
    : ring_(std::make_unique<Cell[]>(kRingCapacity))
{
    for (size_t i = 0; i < kRingCapacity; i++) {
        ring_[i].seq.store(i, std::memory_order_relaxed);
    }
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

MetricsCollector::~MetricsCollector() = default;

void MetricsCollector::record_request(uint16_t status_code,
                                      std::chrono::microseconds latency,
                                      size_t bytes_sent,
                                      size_t bytes_received) {
    total_requests_.fetch_add(1, std::memory_order_relaxed);
    total_bytes_sent_.fetch_add(bytes_sent, std::memory_order_relaxed);
    total_bytes_received_.fetch_add(bytes_received, std::memory_order_relaxed);

    cumulative_latency_us_.fetch_add(latency.count(), std::memory_order_relaxed);

    if (status_code >= 200 && status_code < 400) {
        successful_requests_.fetch_add(1, std::memory_order_relaxed);
    } else {
        failed_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    auto lat_val = latency.count();
    auto min_curr = min_latency_us_.load(std::memory_order_relaxed);
    while (lat_val < min_curr &&
           !min_latency_us_.compare_exchange_weak(min_curr, lat_val,
               std::memory_order_relaxed)) {}

    auto max_curr = max_latency_us_.load(std::memory_order_relaxed);
    while (lat_val > max_curr &&
           !max_latency_us_.compare_exchange_weak(max_curr, lat_val,
               std::memory_order_relaxed)) {}

    // Lock-free MPSC ring buffer push
    uint64_t t = tail_.load(std::memory_order_relaxed);
    while (true) {
        uint64_t h = head_.load(std::memory_order_acquire);
        if (t - h >= kRingCapacity) return; // ring full, drop sample
        if (tail_.compare_exchange_weak(t, t + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            size_t idx = t & kRingMask;
            ring_[idx].value = lat_val;
            ring_[idx].seq.store(t + 1, std::memory_order_release);
            return;
        }
    }
}

RequestMetrics MetricsCollector::snapshot() const {
    RequestMetrics m;
    m.total_requests = total_requests_.load(std::memory_order_relaxed);
    m.successful_requests = successful_requests_.load(std::memory_order_relaxed);
    m.failed_requests = failed_requests_.load(std::memory_order_relaxed);
    m.total_bytes_sent = total_bytes_sent_.load(std::memory_order_relaxed);
    m.total_bytes_received = total_bytes_received_.load(std::memory_order_relaxed);

    auto cum_lat = cumulative_latency_us_.load(std::memory_order_relaxed);
    if (m.total_requests > 0) {
        m.mean_latency_us = static_cast<double>(cum_lat) / m.total_requests;
    }

    m.min_latency = std::chrono::microseconds(
        min_latency_us_.load(std::memory_order_relaxed));
    m.max_latency = std::chrono::microseconds(
        max_latency_us_.load(std::memory_order_relaxed));

    // Drain lock-free ring buffer
    std::vector<int64_t> sorted;
    uint64_t h = head_.load(std::memory_order_relaxed);
    while (true) {
        size_t idx = h & kRingMask;
        uint64_t seq = ring_[idx].seq.load(std::memory_order_acquire);
        if (seq != h + 1) break;
        sorted.push_back(ring_[idx].value);
        ring_[idx].seq.store(h + kRingCapacity, std::memory_order_release);
        h++;
    }
    head_.store(h, std::memory_order_release);

    if (!sorted.empty()) {
        std::sort(sorted.begin(), sorted.end());
        auto p95_idx = static_cast<size_t>(sorted.size() * 0.95);
        auto p99_idx = static_cast<size_t>(sorted.size() * 0.99);
        m.p95_latency_us = static_cast<uint64_t>(sorted[std::min(p95_idx, sorted.size() - 1)]);
        m.p99_latency_us = static_cast<uint64_t>(sorted[std::min(p99_idx, sorted.size() - 1)]);
    }

    return m;
}

double MetricsCollector::requests_per_second() const {
    auto now = std::chrono::steady_clock::now();
    auto start = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(
            start_time_.load(std::memory_order_relaxed)));
    auto elapsed = std::chrono::duration<double>(now - start).count();

    if (elapsed < 0.001) return 0.0;
    return total_requests_.load(std::memory_order_relaxed) / elapsed;
}

double MetricsCollector::error_rate() const {
    auto total = total_requests_.load(std::memory_order_relaxed);
    if (total == 0) return 0.0;
    return static_cast<double>(failed_requests_.load(std::memory_order_relaxed)) / total * 100.0;
}

uint64_t MetricsCollector::p95_latency_us() const {
    return snapshot().p95_latency_us;
}

uint64_t MetricsCollector::p99_latency_us() const {
    return snapshot().p99_latency_us;
}

void MetricsCollector::reset() {
    total_requests_.store(0, std::memory_order_relaxed);
    successful_requests_.store(0, std::memory_order_relaxed);
    failed_requests_.store(0, std::memory_order_relaxed);
    total_bytes_sent_.store(0, std::memory_order_relaxed);
    total_bytes_received_.store(0, std::memory_order_relaxed);
    cumulative_latency_us_.store(0, std::memory_order_relaxed);
    min_latency_us_.store(std::chrono::microseconds::max().count(), std::memory_order_relaxed);
    max_latency_us_.store(std::chrono::microseconds::min().count(), std::memory_order_relaxed);
    start_time_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                      std::memory_order_relaxed);

    // Reinitialize ring buffer
    uint64_t h = head_.load(std::memory_order_relaxed);
    uint64_t t = tail_.load(std::memory_order_relaxed);
    for (uint64_t i = h; i < t; i++) {
        ring_[i & kRingMask].seq.store(i + kRingCapacity, std::memory_order_relaxed);
    }
    head_.store(t, std::memory_order_release);
}

} // namespace cppload::metrics
