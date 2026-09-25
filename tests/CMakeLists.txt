# @author ssrjkk | volley
enable_testing()

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Download and build GTest if not found
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
    )
    FetchContent_MakeAvailable(googletest)
endif()

# Self-contained gtest main (avoids dependency on a distro-provided
# libgtest_main shared library, which some security policies block).
add_library(cppload_gtest_main OBJECT gt_main.cpp)
target_link_libraries(cppload_gtest_main PUBLIC GTest::gtest)

add_executable(test_http_client test_http_client.cpp)
target_link_libraries(test_http_client
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_metrics test_metrics.cpp)
target_link_libraries(test_metrics
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_token_bucket test_token_bucket.cpp)
target_link_libraries(test_token_bucket
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_auth test_auth.cpp)
target_link_libraries(test_auth
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_vault test_vault.cpp)
target_link_libraries(test_vault
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_otlp test_otlp.cpp)
target_link_libraries(test_otlp
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_yaml_parser test_yaml_parser.cpp)
target_link_libraries(test_yaml_parser
    PRIVATE cppload_core cppload_gtest_main
    PRIVATE yaml-cpp::yaml-cpp
)

add_executable(test_url_parse test_url_parse.cpp)
target_link_libraries(test_url_parse
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_result test_result.cpp)
target_link_libraries(test_result
    PRIVATE cppload_core cppload_gtest_main
)

add_executable(test_prometheus test_prometheus.cpp)
target_link_libraries(test_prometheus
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME PrometheusTest COMMAND test_prometheus)

add_executable(test_sharded_metrics test_sharded_metrics.cpp)
target_link_libraries(test_sharded_metrics
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME ShardedMetricsTest COMMAND test_sharded_metrics)

add_executable(test_metrics_stress test_metrics_stress.cpp)
target_link_libraries(test_metrics_stress
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME MetricsStressTest COMMAND test_metrics_stress)

add_executable(test_protocol_factory test_protocol_factory.cpp)
target_link_libraries(test_protocol_factory
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME ProtocolFactoryTest COMMAND test_protocol_factory)

add_executable(test_io_context_pool test_io_context_pool.cpp)
target_link_libraries(test_io_context_pool
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME IoContextPoolTest COMMAND test_io_context_pool)

add_executable(test_tls_context test_tls_context.cpp)
target_link_libraries(test_tls_context
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME TlsContextTest COMMAND test_tls_context)

add_test(NAME HttpClientTest COMMAND test_http_client)
add_test(NAME MetricsTest COMMAND test_metrics)
add_test(NAME TokenBucketTest COMMAND test_token_bucket)
add_test(NAME AuthProviderTest COMMAND test_auth)
add_test(NAME VaultClientTest COMMAND test_vault)
add_test(NAME OtlpExporterTest COMMAND test_otlp)
add_test(NAME YamlParserTest COMMAND test_yaml_parser)
add_test(NAME UrlParseTest COMMAND test_url_parse)
add_test(NAME ResultTest COMMAND test_result)

add_executable(test_cli_args test_cli_args.cpp)
target_link_libraries(test_cli_args
    PRIVATE cppload_core cppload_gtest_main
)
if(TARGET volley)
    target_compile_definitions(test_cli_args
        PRIVATE VOLLEY_TEST_CLI_PATH="$<TARGET_FILE:volley>")
else()
    # No CLI target (e.g. sanitizer-only builds): fall back to the mock script.
    # Absolute paths so the test works regardless of the ctest working dir.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    target_compile_definitions(test_cli_args
        PRIVATE VOLLEY_TEST_CLI_PATH="${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/mock_cli.py")
endif()
add_test(NAME CliArgsTest COMMAND test_cli_args)

add_executable(test_redact test_redact.cpp)
target_link_libraries(test_redact
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME RedactTest COMMAND test_redact)

add_executable(test_term test_term.cpp)
target_link_libraries(test_term
    PRIVATE cppload_core cppload_gtest_main
)
add_test(NAME TermColorTest COMMAND test_term)

add_executable(test_platform test_platform.cpp)
target_link_libraries(test_platform
    PRIVATE cppload_platform cppload_gtest_main
)
add_test(NAME PlatformTest COMMAND test_platform)
set_tests_properties(PlatformTest PROPERTIES TIMEOUT 60)

# Benchmarks (performance regression tests)
if(VOLLEY_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Integration tests (mock server)
if(VOLLEY_BUILD_INTEGRATION_TESTS)
    add_subdirectory(integration)
endif()