// @author ssrjkk | cppload
#include <benchmark/benchmark.h>
#include "cppload/metrics/sharded_collector.hpp"
#include <thread>
#include <vector>

using namespace cppload::metrics;

static void BM_ShardedCollector_RecordRequest(benchmark::State& state) {
    ShardedMetricsCollector collector;
    for (auto _ : state) {
        collector.record_request(200, std::chrono::microseconds(1500), 256, 1024);
    }
}
BENCHMARK(BM_ShardedCollector_RecordRequest);

static void BM_ShardedCollector_Snapshot(benchmark::State& state) {
    ShardedMetricsCollector collector;
    for (int i = 0; i < 10000; ++i) {
        collector.record_request(
            i % 10 == 0 ? 500 : 200,
            std::chrono::microseconds(i % 5000),
            128, 512);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(collector.snapshot());
    }
}
BENCHMARK(BM_ShardedCollector_Snapshot);

static void BM_ShardedCollector_ConcurrentRecord(benchmark::State& state) {
    ShardedMetricsCollector collector;
    const size_t num_threads = std::min<size_t>(state.range(0), std::thread::hardware_concurrency());
    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (size_t t = 0; t < num_threads; ++t) {
            threads.emplace_back([&collector]() {
                for (int i = 0; i < 1000; ++i) {
                    collector.record_request(200, std::chrono::microseconds(i), 64, 256);
                }
            });
        }
        for (auto& th : threads) {
            th.join();
        }
    }
}
BENCHMARK(BM_ShardedCollector_ConcurrentRecord)->Arg(1)->Arg(2)->Arg(4)->Arg(8);

static void BM_ShardedCollectorRequestsPerSecond(benchmark::State& state) {
    ShardedMetricsCollector collector;
    for (int i = 0; i < 1000; ++i) {
        collector.record_request(200, std::chrono::microseconds(100), 128, 512);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(collector.requests_per_second());
    }
}
BENCHMARK(BM_ShardedCollectorRequestsPerSecond);

static void BM_ShardedCollector_ErrorRate(benchmark::State& state) {
    ShardedMetricsCollector collector;
    for (int i = 0; i < 1000; ++i) {
        collector.record_request(i % 10 == 0 ? 500 : 200, std::chrono::microseconds(100), 128, 512);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(collector.error_rate());
    }
}
BENCHMARK(BM_ShardedCollector_ErrorRate);

static void BM_ShardedCollector_Reset(benchmark::State& state) {
    for (auto _ : state) {
        ShardedMetricsCollector collector;
        for (int i = 0; i < 10000; ++i) {
            collector.record_request(200, std::chrono::microseconds(i), 64, 256);
        }
        collector.reset();
    }
}
BENCHMARK(BM_ShardedCollector_Reset);

BENCHMARK_MAIN();
