// @author ssrjkk | cppload
#include <benchmark/benchmark.h>
#include "cppload/core/url_parse.hpp"
#include "cppload/core/url_encode.hpp"

static void BM_ParseUrl_Short(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::parse_url("http://example.com/path"));
    }
}
BENCHMARK(BM_ParseUrl_Short);

static void BM_ParseUrl_Long(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::parse_url("https://api.example.com:8443/v1/users/123/profile?active=true&role=admin"));
    }
}
BENCHMARK(BM_ParseUrl_Long);

static void BM_ParseUrl_NoPath(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::parse_url("https://example.com"));
    }
}
BENCHMARK(BM_ParseUrl_NoPath);

static void BM_SanitizePath(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::sanitize_path("/v1/secrets/data/my-secret"));
    }
}
BENCHMARK(BM_SanitizePath);

static void BM_UrlEncode_Short(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::url_encode("hello world"));
    }
}
BENCHMARK(BM_UrlEncode_Short);

static void BM_UrlEncode_SpecialChars(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::url_encode("key=secret&token=abc123!@#$%"));
    }
}
BENCHMARK(BM_UrlEncode_SpecialChars);

static void BM_UrlEncode_Empty(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(cppload::core::url_encode(""));
    }
}
BENCHMARK(BM_UrlEncode_Empty);

BENCHMARK_MAIN();
