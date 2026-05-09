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
    
    // p95 should be around 1900-2000 (95th percentile of 20 = 19th element ~ 1900)
    auto p95 = collector.p95_latency_us();
    EXPECT_GE(p95, 1800);
    EXPECT_LE(p95, 2100);
    
    // p99 should be the max (2000)
    auto p99 = collector.p99_latency_us();
    EXPECT_GE(p99, 1900);
    EXPECT_LE(p99, 2100);
}

TEST(MetricsCollectorTest, PercentilesSingleValue) {
    cppload::metrics::MetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(500), 100, 500);
    EXPECT_EQ(collector.p95_latency_us(), 500);
    EXPECT_EQ(collector.p99_latency_us(), 500);
}

TEST(MetricsCollectorTest, PercentilesZeroRequests) {
    cppload::metrics::MetricsCollector collector;
    EXPECT_EQ(collector.p95_latency_us(), 0);
    EXPECT_EQ(collector.p99_latency_us(), 0);
}
