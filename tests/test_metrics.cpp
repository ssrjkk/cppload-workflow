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
