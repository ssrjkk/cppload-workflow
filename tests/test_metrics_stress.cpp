#include <gtest/gtest.h>
#include "cppload/metrics/collector.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <numeric>

static constexpr int kNumThreads = 100;
static constexpr int kRecordsPerThread = 10000;
static constexpr int64_t kExpectedTotal = kNumThreads * kRecordsPerThread;

TEST(MetricsCollectorStressTest, ConcurrentRecordAndSnapshot) {
    cppload::metrics::MetricsCollector collector;
    std::atomic<bool> stop{false};
    std::atomic<int64_t> snapshot_count{0};
    std::atomic<int64_t> inconsistent_snapshots{0};

    std::vector<std::thread> writers;
    for (int t = 0; t < kNumThreads; ++t) {
        writers.emplace_back([&collector, t]() {
            for (int i = 0; i < kRecordsPerThread; ++i) {
                collector.record_request(
                    200,
                    std::chrono::microseconds(100 + i % 1000),
                    512,
                    1024);
            }
        });
    }

    std::thread reader([&]() {
        while (!stop.load(std::memory_order_relaxed)) {
            auto m = collector.snapshot();
            snapshot_count.fetch_add(1, std::memory_order_relaxed);
            if (m.total_requests > kExpectedTotal) {
                inconsistent_snapshots.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    for (auto& w : writers) w.join();
    stop.store(true, std::memory_order_relaxed);
    reader.join();

    auto final_m = collector.snapshot();
    EXPECT_EQ(final_m.total_requests, kExpectedTotal)
        << "Lost " << kExpectedTotal - final_m.total_requests << " requests out of " << kExpectedTotal;
    EXPECT_EQ(inconsistent_snapshots.load(), 0)
        << "Found " << inconsistent_snapshots.load() << " inconsistent snapshots out of " << snapshot_count.load();
}

TEST(MetricsCollectorStressTest, ConcurrentRecordAndReset) {
    cppload::metrics::MetricsCollector collector;
    std::atomic<bool> stop{false};
    std::atomic<int64_t> reset_count{0};

    std::vector<std::thread> writers;
    for (int t = 0; t < 10; ++t) {
        writers.emplace_back([&collector, &stop]() {
            while (!stop.load(std::memory_order_relaxed)) {
                collector.record_request(200, std::chrono::microseconds(100), 100, 200);
            }
        });
    }

    std::thread reseter([&]() {
        for (int i = 0; i < 100; ++i) {
            collector.reset();
            reset_count.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop.store(true, std::memory_order_relaxed);

    for (auto& w : writers) w.join();
    reseter.join();

    auto m = collector.snapshot();
    // After concurrent writes + resets, verify no crash and plausible count
    EXPECT_LE(m.total_requests, static_cast<int64_t>(10) * 500000);
}

TEST(MetricsCollectorStressTest, MinMaxLatencyCorrectness) {
    cppload::metrics::MetricsCollector collector;

    std::vector<std::thread> writers;
    for (int t = 0; t < kNumThreads; ++t) {
        writers.emplace_back([&collector, t]() {
            for (int i = 0; i < kRecordsPerThread; ++i) {
                auto lat_us = static_cast<int64_t>((t * kRecordsPerThread + i) % 100000);
                collector.record_request(
                    200,
                    std::chrono::microseconds(lat_us),
                    100,
                    200);
            }
        });
    }

    for (auto& w : writers) w.join();

    auto m = collector.snapshot();
    EXPECT_EQ(m.total_requests, kExpectedTotal);
    EXPECT_LE(m.min_latency.count(), 0);
    EXPECT_GE(m.max_latency.count(), 99999);
}
