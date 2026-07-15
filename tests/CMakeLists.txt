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

add_executable(test_token_bucket test_token_bucket.cpp)
target_link_libraries(test_token_bucket
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_auth test_auth.cpp)
target_link_libraries(test_auth
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_vault test_vault.cpp)
target_link_libraries(test_vault
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_otlp test_otlp.cpp)
target_link_libraries(test_otlp
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_yaml_parser test_yaml_parser.cpp)
target_link_libraries(test_yaml_parser
    PRIVATE cppload_core GTest::gtest_main
    PRIVATE yaml-cpp::yaml-cpp
)

add_executable(test_prometheus test_prometheus.cpp)
target_link_libraries(test_prometheus
    PRIVATE cppload_core GTest::gtest_main
)
add_test(NAME PrometheusTest COMMAND test_prometheus)

add_test(NAME HttpClientTest COMMAND test_http_client)
add_test(NAME MetricsTest COMMAND test_metrics)
add_test(NAME TokenBucketTest COMMAND test_token_bucket)
add_test(NAME AuthProviderTest COMMAND test_auth)
add_test(NAME VaultClientTest COMMAND test_vault)
add_test(NAME OtlpExporterTest COMMAND test_otlp)
add_test(NAME YamlParserTest COMMAND test_yaml_parser)

# Benchmarks (performance regression tests)
if(CPLOAD_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Integration tests (mock server)
if(CPLOAD_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()
