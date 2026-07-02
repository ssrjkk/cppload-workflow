#include <benchmark/benchmark.h>
#include "cppload/core/token_bucket.hpp"
#include <thread>
#include <vector>
#include <atomic>

static void BM_TokenBucket_Consume(benchmark::State& state) {
    cppload::TokenBucket bucket(100000.0);
    for (auto _ : state) {
        bucket.consume();
    }
}
BENCHMARK(BM_TokenBucket_Consume);

static void BM_TokenBucket_TryConsume(benchmark::State& state) {
    cppload::TokenBucket bucket(100000.0);
    for (auto _ : state) {
        benchmark::DoNotOptimize(bucket.try_consume());
    }
}
BENCHMARK(BM_TokenBucket_TryConsume);

static void BM_TokenBucket_Parallel(benchmark::State& state) {
    cppload::TokenBucket bucket(500000.0);
    for (auto _ : state) {
        std::vector<std::thread> threads;
        std::atomic<int64_t> consumed{0};
        for (int t = 0; t < state.range(0); ++t) {
            threads.emplace_back([&]() {
                for (int i = 0; i < 1000; ++i) {
                    if (bucket.try_consume()) consumed.fetch_add(1);
                }
            });
        }
        for (auto& t : threads) t.join();
        benchmark::DoNotOptimize(consumed.load());
    }
}
BENCHMARK(BM_TokenBucket_Parallel)->Arg(2)->Arg(4)->Arg(8);

BENCHMARK_MAIN();
