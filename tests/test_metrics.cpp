// @author ssrjkk | volley
#include <gtest/gtest.h>
#include "cppload/metrics/collector.hpp"
#include <thread>

TEST(MetricsCollectorTest, RecordsRequests) {
    cppload::metrics::MetricsCollector collector;
    
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    collector.record_request(201, std::chrono::microseconds(150), 120, 600);
    
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 2ull);
    EXPECT_EQ(m.successful_requests, 2ull);
    EXPECT_EQ(m.failed_requests, 0ull);
}

TEST(MetricsCollectorTest, CalculatesErrorRate) {
    cppload::metrics::MetricsCollector collector;
    
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    collector.record_request(500, std::chrono::microseconds(200), 100, 200);
    
    EXPECT_DOUBLE_EQ(collector.error_rate(), 50.0);
}

TEST(MetricsCollectorTest, TracksLatency) {
    cppload::metrics::MetricsCollector collector;
    
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    collector.record_request(200, std::chrono::microseconds(300), 100, 500);
    
    auto m = collector.snapshot();
    EXPECT_GE(m.max_latency.count(), 300);
    EXPECT_LE(m.min_latency.count(), 100);
}

TEST(MetricsCollectorTest, Percentiles) {
    cppload::metrics::MetricsCollector collector;
    
    // 20 requests with latencies from 100 to 2000 us
    for (int i = 0; i < 20; ++i) {
        collector.record_request(200, std::chrono::microseconds(100 * (i + 1)), 100, 500);
    }
    
    auto m = collector.snapshot();
    // p95: index = 20 * 0.95 = 19 → sorted[19] = 2000
    EXPECT_EQ(m.p95_latency_us, 2000ull);
    // p99: index = 20 * 0.99 = 19 → sorted[19] = 2000
    EXPECT_EQ(m.p99_latency_us, 2000ull);
}

TEST(MetricsCollectorTest, PercentilesSingleValue) {
    cppload::metrics::MetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(500), 100, 500);
    auto m = collector.snapshot();
    EXPECT_EQ(m.p95_latency_us, 500ull);
    EXPECT_EQ(m.p99_latency_us, 500ull);
}

TEST(MetricsCollectorTest, SnapshotIsNonDestructive) {
    cppload::metrics::MetricsCollector collector;
    for (int i = 1; i <= 100; ++i) {
        collector.record_request(200, std::chrono::microseconds(i * 10), 100, 500);
    }

    auto first = collector.snapshot();
    EXPECT_NE(first.p95_latency_us, 0u);
    EXPECT_NE(first.p99_latency_us, 0u);

    // A second snapshot must observe the same buffered samples: snapshot()
    // must not drain the ring buffer (previously it zeroed p95/p99).
    auto second = collector.snapshot();
    EXPECT_EQ(second.p95_latency_us, first.p95_latency_us);
    EXPECT_EQ(second.p99_latency_us, first.p99_latency_us);
    EXPECT_EQ(second.total_requests, 100u);

    // Percentiles still see the same samples after snapshot() ran.
    EXPECT_EQ(collector.percentile(1.0), 1000u);
    EXPECT_GT(collector.percentile(0.5), 0u);
}

TEST(MetricsCollectorTest, PercentilesZeroRequests) {
    cppload::metrics::MetricsCollector collector;
    auto m = collector.snapshot();
    EXPECT_EQ(m.p95_latency_us, 0ull);
    EXPECT_EQ(m.p99_latency_us, 0ull);
}

TEST(MetricsCollectorTest, SnapshotZeroNoRequests) {
    cppload::metrics::MetricsCollector collector;
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 0ull);
    EXPECT_EQ(m.p95_latency_us, 0ull);
    EXPECT_EQ(m.p99_latency_us, 0ull);
}

TEST(MetricsCollectorTest, RequestsPerSecond) {
    cppload::metrics::MetricsCollector collector;
    EXPECT_DOUBLE_EQ(collector.requests_per_second(), 0.0);
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    // requests_per_second() returns 0 until kMinElapsedSeconds (1ms) have
    // passed since construction; step over that window before asserting.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // After one request, RPS should be > 0
    EXPECT_GT(collector.requests_per_second(), 0.0);
}

TEST(MetricsCollectorTest, ResetClearsCounters) {
    cppload::metrics::MetricsCollector collector;
    collector.record_request(500, std::chrono::microseconds(200), 100, 200);
    EXPECT_EQ(collector.snapshot().total_requests, 1ull);
    collector.reset();
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 0ull);
    EXPECT_EQ(m.failed_requests, 0ull);
    EXPECT_EQ(m.p95_latency_us, 0ull);
}

TEST(MetricsCollectorTest, ArbitraryPercentile) {
    cppload::metrics::MetricsCollector collector;
    for (int i = 1; i <= 100; ++i) {
        collector.record_request(200, std::chrono::microseconds(i), 100, 500);
    }
    EXPECT_EQ(collector.percentile(0.0), 1u);
    EXPECT_EQ(collector.percentile(1.0), 100u);
    EXPECT_GE(collector.percentile(0.5), 49u);
    EXPECT_LE(collector.percentile(0.5), 51u);
    EXPECT_GE(collector.percentile(0.95), 94u);
    EXPECT_LE(collector.percentile(0.95), 96u);
}

TEST(MetricsCollectorTest, PercentileEmpty) {
    cppload::metrics::MetricsCollector collector;
    EXPECT_EQ(collector.percentile(0.5), 0u);
}

TEST(MetricsCollectorTest, PercentileOutOfBounds) {
    cppload::metrics::MetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    EXPECT_EQ(collector.percentile(-0.1), 0u);
    EXPECT_EQ(collector.percentile(1.1), 0u);
}

TEST(MetricsCollectorTest, RingEvictsOldestAfterCapacity) {
    cppload::metrics::MetricsCollector collector;
    // Fill the ring past its capacity: the oldest samples must be evicted
    // instead of silently dropping new ones (previously percentiles froze on
    // the first 1M requests because head_ never advanced).
    constexpr size_t kRingCapacity = 1u << 20;
    constexpr size_t kExtra = 5000;
    constexpr size_t kTotal = kRingCapacity + kExtra;
    for (size_t i = 1; i <= kTotal; ++i) {
        collector.record_request(
            200, std::chrono::microseconds(static_cast<int64_t>(i)), 100, 500);
    }

    // The most recent sample must be visible.
    EXPECT_EQ(collector.percentile(1.0), static_cast<uint64_t>(kTotal));
    // And percentiles must reflect the recent window, not the first 1M
    // requests buffered before the ring filled up.
    auto m = collector.snapshot();
    EXPECT_GT(m.p95_latency_us, static_cast<uint64_t>(kRingCapacity));
}