// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/metrics/collector.hpp"
#include <thread>

TEST(MetricsCollectorTest, RecordsRequests) {
    cppload::metrics::MetricsCollector collector;
    
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    collector.record_request(201, std::chrono::microseconds(150), 120, 600);
    
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 2);
    EXPECT_EQ(m.successful_requests, 2);
    EXPECT_EQ(m.failed_requests, 0);
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
    EXPECT_EQ(m.p95_latency_us, 2000);
    // p99: index = 20 * 0.99 = 19 → sorted[19] = 2000
    EXPECT_EQ(m.p99_latency_us, 2000);
}

TEST(MetricsCollectorTest, PercentilesSingleValue) {
    cppload::metrics::MetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(500), 100, 500);
    auto m = collector.snapshot();
    EXPECT_EQ(m.p95_latency_us, 500);
    EXPECT_EQ(m.p99_latency_us, 500);
}

TEST(MetricsCollectorTest, PercentilesZeroRequests) {
    cppload::metrics::MetricsCollector collector;
    auto m = collector.snapshot();
    EXPECT_EQ(m.p95_latency_us, 0);
    EXPECT_EQ(m.p99_latency_us, 0);
}

TEST(MetricsCollectorTest, SnapshotZeroNoRequests) {
    cppload::metrics::MetricsCollector collector;
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 0);
    EXPECT_EQ(m.p95_latency_us, 0);
    EXPECT_EQ(m.p99_latency_us, 0);
}

TEST(MetricsCollectorTest, RequestsPerSecond) {
    cppload::metrics::MetricsCollector collector;
    EXPECT_DOUBLE_EQ(collector.requests_per_second(), 0.0);
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    // After one request, RPS should be > 0
    EXPECT_GT(collector.requests_per_second(), 0.0);
}

TEST(MetricsCollectorTest, ResetClearsCounters) {
    cppload::metrics::MetricsCollector collector;
    collector.record_request(500, std::chrono::microseconds(200), 100, 200);
    EXPECT_EQ(collector.snapshot().total_requests, 1);
    collector.reset();
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 0);
    EXPECT_EQ(m.failed_requests, 0);
    EXPECT_EQ(m.p95_latency_us, 0);
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