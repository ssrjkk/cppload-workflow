enable_testing()

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Download and build GTest if not found
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(test_http_client test_http_client.cpp)
target_link_libraries(test_http_client
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_metrics test_metrics.cpp)
target_link_libraries(test_metrics
    PRIVATE cppload_core GTest::gtest_main
)

if(prometheus-cpp_FOUND)
    add_executable(test_prometheus test_prometheus.cpp)
    target_link_libraries(test_prometheus
        PRIVATE cppload_core GTest::gtest_main
    )
    add_test(NAME PrometheusTest COMMAND test_prometheus)
endif()

add_test(NAME HttpClientTest COMMAND test_http_client)
add_test(NAME MetricsTest COMMAND test_metrics)
