#include <gtest/gtest.h>
#include "cppload/metrics/sharded_collector.hpp"
#include <thread>
#include <vector>
#include <numeric>

TEST(ShardedMetricsCollectorTest, ConstructAndDestroy) {
    ASSERT_NO_THROW({
        cppload::metrics::ShardedMetricsCollector collector;
    });
}

TEST(ShardedMetricsCollectorTest, RecordRequest) {
    cppload::metrics::ShardedMetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(100), 512, 1024);
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 1);
    EXPECT_EQ(m.successful_requests, 1);
    EXPECT_EQ(m.failed_requests, 0);
    EXPECT_EQ(m.total_bytes_sent, 512);
    EXPECT_EQ(m.total_bytes_received, 1024);
}

TEST(ShardedMetricsCollectorTest, ErrorCounting) {
    cppload::metrics::ShardedMetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(100), 100, 200);
    collector.record_request(500, std::chrono::microseconds(200), 100, 200);
    collector.record_request(404, std::chrono::microseconds(50), 100, 200);
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 3);
    EXPECT_EQ(m.successful_requests, 1);
    EXPECT_EQ(m.failed_requests, 2);
    EXPECT_DOUBLE_EQ(collector.error_rate(), 2.0 / 3.0 * 100.0);
}

TEST(ShardedMetricsCollectorTest, LatencyMinMax) {
    cppload::metrics::ShardedMetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(50), 100, 200);
    collector.record_request(200, std::chrono::microseconds(5000), 100, 200);
    auto m = collector.snapshot();
    EXPECT_LE(m.min_latency_us, 50);
    EXPECT_GE(m.max_latency_us, 5000);
}

TEST(ShardedMetricsCollectorTest, MeanLatency) {
    cppload::metrics::ShardedMetricsCollector collector;
    collector.record_request(200, std::chrono::microseconds(100), 100, 200);
    collector.record_request(200, std::chrono::microseconds(300), 100, 200);
    auto m = collector.snapshot();
    EXPECT_DOUBLE_EQ(m.mean_latency_us, 200.0);
}

TEST(ShardedMetricsCollectorTest, Percentiles) {
    cppload::metrics::ShardedMetricsCollector collector;
    for (int i = 1; i <= 100; ++i) {
        collector.record_request(200, std::chrono::microseconds(i * 100), 100, 200);
    }
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 100);
    EXPECT_GE(m.p95_latency_us, 9400u);
    EXPECT_LE(m.p95_latency_us, 10000u);
    EXPECT_GE(m.p99_latency_us, 9800u);
    EXPECT_LE(m.p99_latency_us, 10000u);
}

TEST(ShardedMetricsCollectorTest, RequestsPerSecond) {
    cppload::metrics::ShardedMetricsCollector collector;
    EXPECT_DOUBLE_EQ(collector.requests_per_second(), 0.0);
    collector.record_request(200, std::chrono::microseconds(100), 100, 200);
    EXPECT_GT(collector.requests_per_second(), 0.0);
}

TEST(ShardedMetricsCollectorTest, Reset) {
    cppload::metrics::ShardedMetricsCollector collector;
    collector.record_request(500, std::chrono::microseconds(200), 100, 200);
    EXPECT_EQ(collector.snapshot().total_requests, 1);
    collector.reset();
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 0);
    EXPECT_EQ(m.failed_requests, 0);
    EXPECT_EQ(m.p95_latency_us, 0);
}

TEST(ShardedMetricsCollectorTest, SnapshotZeroRequests) {
    cppload::metrics::ShardedMetricsCollector collector;
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 0);
    EXPECT_EQ(m.p95_latency_us, 0);
    EXPECT_EQ(m.p99_latency_us, 0);
}

TEST(ShardedMetricsCollectorTest, ConcurrentRecord) {
    cppload::metrics::ShardedMetricsCollector collector;
    constexpr int kThreads = 32;
    constexpr int kPerThread = 10000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&collector]() {
            for (int i = 0; i < kPerThread; ++i) {
                collector.record_request(
                    200,
                    std::chrono::microseconds(100 + (i % 500)),
                    100,
                    200);
            }
        });
    }

    for (auto& t : threads) t.join();

    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, kThreads * kPerThread);
    EXPECT_EQ(m.successful_requests, kThreads * kPerThread);
    EXPECT_EQ(m.failed_requests, 0);
    EXPECT_EQ(m.min_latency_us, 100u);
    EXPECT_GE(m.max_latency_us, 500u);
}

TEST(ShardedMetricsCollectorTest, ConcurrentRecordAndSnapshot) {
    cppload::metrics::ShardedMetricsCollector collector;
    constexpr int kWriters = 16;
    constexpr int kPerWriter = 50000;
    std::atomic<bool> stop{false};

    std::vector<std::thread> writers;
    for (int t = 0; t < kWriters; ++t) {
        writers.emplace_back([&collector]() {
            for (int i = 0; i < kPerWriter; ++i) {
                collector.record_request(200, std::chrono::microseconds(i % 1000), 100, 200);
            }
        });
    }

    std::thread reader([&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            auto m = collector.snapshot();
            EXPECT_LE(m.total_requests, kWriters * kPerWriter);
        }
    });

    for (auto& w : writers) w.join();
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, kWriters * kPerWriter);
}

TEST(ShardedMetricsCollectorTest, ConcurrentRecordAndReset) {
    cppload::metrics::ShardedMetricsCollector collector;
    std::atomic<bool> stop{false};

    std::vector<std::thread> writers;
    for (int t = 0; t < 8; ++t) {
        writers.emplace_back([&collector, &stop]() {
            while (!stop.load(std::memory_order_relaxed)) {
                collector.record_request(200, std::chrono::microseconds(100), 100, 200);
            }
        });
    }

    std::thread reseter([&]() {
        for (int i = 0; i < 100; ++i) {
            collector.reset();
            std::this_thread::yield();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop.store(true, std::memory_order_relaxed);

    for (auto& w : writers) w.join();
    reseter.join();

    auto m = collector.snapshot();
    // After concurrent writes + resets, total is non-negative by type
    // but we verify no crash occurred and count is plausible
    EXPECT_LE(m.total_requests, static_cast<uint64_t>(8) * 50000);
}
