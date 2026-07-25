# @author ssrjkk | cppload
find_package(benchmark QUIET)
if(NOT benchmark_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googlebenchmark)
endif()

add_executable(token_bucket_bench token_bucket_bench.cpp)
target_link_libraries(token_bucket_bench PRIVATE cppload_core benchmark::benchmark)
add_test(NAME TokenBucketBench COMMAND token_bucket_bench)

add_executable(metrics_bench metrics_bench.cpp)
target_link_libraries(metrics_bench PRIVATE cppload_core benchmark::benchmark)
add_test(NAME MetricsBench COMMAND metrics_bench)

add_executable(http_client_bench http_client_bench.cpp)
target_link_libraries(http_client_bench PRIVATE cppload_core benchmark::benchmark)
add_test(NAME HttpClientBench COMMAND http_client_bench)

add_executable(url_bench url_bench.cpp)
target_link_libraries(url_bench PRIVATE cppload_core benchmark::benchmark)
add_test(NAME UrlBench COMMAND url_bench)