#include <benchmark/benchmark.h>
#include "cppload/net/http_client.hpp"
#include "cppload/net/connection_pool.hpp"
#include <boost/asio/io_context.hpp>
#include <memory>

static void BM_HttpClient_Construct(benchmark::State& state) {
    boost::asio::io_context ioc;
    for (auto _ : state) {
        cppload::net::HttpClient client(ioc);
        benchmark::DoNotOptimize(&client);
    }
}
BENCHMARK(BM_HttpClient_Construct);

static void BM_ConnectionPool_AcquireRelease(benchmark::State& state) {
    boost::asio::io_context ioc;
    cppload::net::PoolConfig cfg;
    cfg.min_connections = 10;
    cfg.max_connections = 100;
    cppload::net::ConnectionPool pool(ioc, cfg);

    for (auto _ : state) {
        auto client = pool.acquire("localhost", "8080");
        if (client) {
            pool.release(std::move(client), "localhost", "8080");
        }
        benchmark::DoNotOptimize(client);
    }
}
BENCHMARK(BM_ConnectionPool_AcquireRelease);

static void BM_ConnectionPool_Stats(benchmark::State& state) {
    boost::asio::io_context ioc;
    cppload::net::ConnectionPool pool(ioc);

    for (int i = 0; i < 10; ++i) {
        auto c = pool.acquire("testhost", "9090");
        if (c) pool.release(std::move(c), "testhost", "9090");
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(pool.stats());
    }
}
BENCHMARK(BM_ConnectionPool_Stats);

BENCHMARK_MAIN();
