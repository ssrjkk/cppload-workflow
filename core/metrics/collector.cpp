// @author ssrjkk | cppload
#include "cppload/metrics/collector.hpp"
#include <algorithm>

namespace cppload::metrics {

namespace {
    constexpr double kP95 = 0.95;
    constexpr double kP99 = 0.99;
    constexpr double kMinElapsedSeconds = 0.001;
    // Upper bound on samples copied out of the ring per snapshot. Snapshot()
    // is called on a timer (e.g. Prometheus, results), so the hit cost must
    // stay bounded even when the 1M-slot ring is full.
    constexpr uint64_t kMaxSnapshotSamples = 100000;
}

MetricsCollector::MetricsCollector()
    : ring_(std::make_unique<Cell[]>(kRingCapacity))
{
    for (size_t i = 0; i < kRingCapacity; i++) {
        ring_[i].seq.store(i, std::memory_order_relaxed);
    }
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

MetricsCollector::~MetricsCollector() noexcept = default;

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

    // Lock-free MPSC ring buffer push with drop-oldest eviction.
    // When the ring is full the oldest *published* slot is evicted so a
    // long-running test keeps tracking the recent window instead of freezing
    // percentiles on the first kRingCapacity samples. Slots whose payload is
    // still being written (seq == i) must not be evicted: they will be
    // published momentarily, so we simply retry.
    uint64_t t = tail_.load(std::memory_order_relaxed);
    while (true) {
        uint64_t h = head_.load(std::memory_order_acquire);
        if (t - h >= kRingCapacity) {
            size_t old_idx = h & kRingMask;
            if (ring_[old_idx].seq.load(std::memory_order_acquire) != h + 1) {
                // Oldest slot is mid-write; refresh tail (other writers keep
                // advancing it) so the eviction check below makes progress.
                t = tail_.load(std::memory_order_relaxed);
                continue;
            }
            if (head_.compare_exchange_weak(h, h + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                // head advanced; reload tail so we never race a fast-moving
                // producer forever with a stale t - h >= capacity test.
                t = tail_.load(std::memory_order_relaxed);
                continue;
            }
            t = tail_.load(std::memory_order_relaxed);
            continue;
        }
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

    // Read-only: the ring is not consumed, so consecutive snapshots keep
    // returning the same buffered samples and never zero out p95/p99.
    auto sorted = collect_ring_samples();
    if (!sorted.empty()) {
        // Nth_element partitions instead of fully sorting: for latency
        // histograms we only need two order statistics, and the first pass
        // also bounds the second (p95 index lies inside [0, p99_idx)).
        auto p95_idx = static_cast<size_t>(sorted.size() * kP95);
        auto p99_idx = static_cast<size_t>(sorted.size() * kP99);
        p95_idx = std::min(p95_idx, sorted.size() - 1);
        p99_idx = std::min(p99_idx, sorted.size() - 1);
        if (p99_idx > p95_idx) {
            std::nth_element(sorted.begin(), sorted.begin() + p99_idx, sorted.end());
            std::nth_element(sorted.begin(), sorted.begin() + p95_idx,
                             sorted.begin() + p99_idx);
            m.p99_latency_us = static_cast<uint64_t>(sorted[p99_idx]);
        } else {
            std::nth_element(sorted.begin(), sorted.begin() + p95_idx, sorted.end());
            m.p99_latency_us = static_cast<uint64_t>(sorted[p95_idx]);
        }
        m.p95_latency_us = static_cast<uint64_t>(sorted[p95_idx]);
    }

    return m;
}

std::vector<int64_t> MetricsCollector::collect_ring_samples() const {
    // Only the head/tail bookkeeping is serialized; the scan itself runs
    // lock-free (cells are published via the seq barrier). Holding the lock
    // while copying up to kMaxSnapshotSamples entries made snapshot() stall
    // writers behind the mutex (audit item #3).
    uint64_t h = 0, t = 0;
    {
        std::lock_guard<std::mutex> lock(snapshot_mtx_);
        h = head_.load(std::memory_order_acquire);
        t = tail_.load(std::memory_order_acquire);
    }
    std::vector<int64_t> samples;
    if (t > h) {
        // Cap the window to the most recent samples so a full 1M-slot ring
        // never forces a giant copy + sort on every snapshot() call.
        uint64_t start = (t - h > kMaxSnapshotSamples) ? t - kMaxSnapshotSamples : h;
        samples.reserve(static_cast<size_t>(t - start));
        for (uint64_t i = start; i < t; ++i) {
            size_t idx = i & kRingMask;
            uint64_t seq = ring_[idx].seq.load(std::memory_order_acquire);
            if (seq != i + 1) break;
            samples.push_back(ring_[idx].value);
        }
    }
    return samples;
}

double MetricsCollector::requests_per_second() const {
    auto now = std::chrono::steady_clock::now();
    auto start = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(
            start_time_.load(std::memory_order_relaxed)));
    auto elapsed = std::chrono::duration<double>(now - start).count();

    if (elapsed < kMinElapsedSeconds) return 0.0;
    return total_requests_.load(std::memory_order_relaxed) / elapsed;
}

double MetricsCollector::error_rate() const {
    auto total = total_requests_.load(std::memory_order_relaxed);
    if (total == 0) return 0.0;
    return static_cast<double>(failed_requests_.load(std::memory_order_relaxed)) / total * 100.0;
}

uint64_t MetricsCollector::percentile(double p) const {
    if (p < 0.0 || p > 1.0) return 0;

    auto samples = collect_ring_samples();
    if (samples.empty()) return 0;
    auto idx = static_cast<size_t>(samples.size() * p);
    idx = std::min(idx, samples.size() - 1);
    std::nth_element(samples.begin(), samples.begin() + idx, samples.end());
    return static_cast<uint64_t>(samples[idx]);
}

void MetricsCollector::reset() {
    std::lock_guard<std::mutex> lock(snapshot_mtx_);
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