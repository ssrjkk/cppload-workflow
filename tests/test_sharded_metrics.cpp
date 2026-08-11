// @author ssrjkk | cppload
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
    // With a bounded reservoir (no bucketing), percentiles are exact for the
    // retained samples. 100 evenly spaced latencies: p95 = 9500us, p99 = 9900us.
    cppload::metrics::ShardedMetricsCollector collector;
    for (int i = 1; i <= 100; ++i) {
        collector.record_request(200, std::chrono::microseconds(i * 100), 100, 200);
    }
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, 100);
    EXPECT_EQ(m.min_latency_us, 100u);
    EXPECT_EQ(m.max_latency_us, 10000u);
    EXPECT_DOUBLE_EQ(m.mean_latency_us, 5050.0);
    EXPECT_EQ(m.p95_latency_us, 9500u);
    EXPECT_EQ(m.p99_latency_us, 9900u);
}

TEST(ShardedMetricsCollectorTest, PercentilesRingOverflow) {
    // More samples than the reservoir cap: percentiles stay within [min, max]
    // and never exceed the largest retained sample.
    constexpr size_t kSamples = cppload::metrics::ShardedMetricsCollector::kMaxLatencySamples + 1000;
    cppload::metrics::ShardedMetricsCollector collector;
    for (size_t i = 0; i < kSamples; ++i) {
        collector.record_request(200, std::chrono::microseconds(500 + (i % 50)), 100, 200);
    }
    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, kSamples);
    EXPECT_EQ(m.min_latency_us, 500u);
    EXPECT_EQ(m.max_latency_us, 549u);
    EXPECT_GE(m.p95_latency_us, m.min_latency_us);
    EXPECT_LE(m.p95_latency_us, m.max_latency_us);
    EXPECT_GE(m.p99_latency_us, m.p95_latency_us);
    EXPECT_LE(m.p99_latency_us, m.max_latency_us);
}

TEST(ShardedMetricsCollectorTest, RequestsPerSecond) {
    cppload::metrics::ShardedMetricsCollector collector;
    EXPECT_DOUBLE_EQ(collector.requests_per_second(), 0.0);
    collector.record_request(200, std::chrono::microseconds(100), 100, 200);
    EXPECT_GT(collector.requests_per_second(), 0.0);

    // After an idle period of >= 1s the window rolls over and the fresh
    // window reports ~0 until a new request arrives.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    double after_idle = collector.requests_per_second();
    EXPECT_GT(after_idle, 0.0);
    EXPECT_LT(after_idle, 1.0);
    EXPECT_NEAR(collector.requests_per_second(), 0.0, 0.001);

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
    constexpr int kThreads = 8;
    constexpr int kPerThread = 20000;

    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&collector]() {
            for (int i = 0; i < kPerThread; ++i) {
                collector.record_request(200, std::chrono::microseconds(100), 100, 200);
            }
        });
    }

    std::thread reseter([&]() {
        for (int i = 0; i < 50; ++i) {
            collector.reset();
            std::this_thread::yield();
        }
    });

    for (auto& w : writers) w.join();
    reseter.join();

    auto m = collector.snapshot();
    // Resets may only zero the counters, so the total can never exceed the
    // fixed number of recorded requests, regardless of scheduling.
    EXPECT_LE(m.total_requests, static_cast<uint64_t>(kThreads) * kPerThread);
}