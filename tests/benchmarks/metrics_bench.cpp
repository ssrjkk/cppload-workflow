// @author ssrjkk | cppload
#include <benchmark/benchmark.h>
#include "cppload/metrics/collector.hpp"
#include <thread>
#include <vector>

static void BM_Metrics_RecordRequest(benchmark::State& state) {
    cppload::metrics::MetricsCollector collector;
    for (auto _ : state) {
        collector.record_request(200, std::chrono::microseconds(100), 512, 1024);
    }
}
BENCHMARK(BM_Metrics_RecordRequest);

static void BM_Metrics_Snapshot(benchmark::State& state) {
    cppload::metrics::MetricsCollector collector;
    for (int i = 0; i < 10000; ++i) {
        collector.record_request(200, std::chrono::microseconds(i), 512, 1024);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(collector.snapshot());
    }
}
BENCHMARK(BM_Metrics_Snapshot);

static void BM_Metrics_ParallelRecord(benchmark::State& state) {
    for (auto _ : state) {
        cppload::metrics::MetricsCollector collector;
        std::vector<std::thread> threads;
        for (int t = 0; t < state.range(0); ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < 10000; ++i) {
                    collector.record_request(200, std::chrono::microseconds(i), 512, 1024);
                }
            });
        }
        for (auto& t : threads) t.join();
        benchmark::DoNotOptimize(collector.snapshot());
    }
}
BENCHMARK(BM_Metrics_ParallelRecord)->Arg(2)->Arg(4)->Arg(8);

BENCHMARK_MAIN();