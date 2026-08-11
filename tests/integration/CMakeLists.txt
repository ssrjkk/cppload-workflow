# @author ssrjkk | cppload
add_executable(test_http_client_integration test_http_client_integration.cpp)
target_link_libraries(test_http_client_integration
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_vault_integration test_vault_integration.cpp)
target_link_libraries(test_vault_integration
    PRIVATE cppload_core GTest::gtest_main nlohmann_json::nlohmann_json
)

add_executable(test_oauth2_integration test_oauth2_integration.cpp)
target_link_libraries(test_oauth2_integration
    PRIVATE cppload_core GTest::gtest_main nlohmann_json::nlohmann_json
)

add_executable(test_connection_pool_integration test_connection_pool_integration.cpp)
target_link_libraries(test_connection_pool_integration
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_engine_integration test_engine_integration.cpp)
target_link_libraries(test_engine_integration
    PRIVATE cppload_core GTest::gtest_main
)

# Self-signed certificates for TLS integration tests. Generated once at
# configure time; tests skip gracefully if openssl is unavailable.
find_program(CPPLOAD_OPENSSL_EXECUTABLE openssl)
set(CPPLOAD_TEST_CERT_DIR "${CMAKE_CURRENT_BINARY_DIR}/certs")
if(CPPLOAD_OPENSSL_EXECUTABLE)
    file(MAKE_DIRECTORY "${CPPLOAD_TEST_CERT_DIR}")
    execute_process(
        COMMAND ${CPPLOAD_OPENSSL_EXECUTABLE} req -x509 -newkey rsa:2048
            -keyout "${CPPLOAD_TEST_CERT_DIR}/server.key"
            -out "${CPPLOAD_TEST_CERT_DIR}/server.pem"
            -days 3650 -nodes -subj "/CN=cppload.test"
            -addext "subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        RESULT_VARIABLE CPPLOAD_GEN_SERVER_RC
        OUTPUT_QUIET ERROR_QUIET)
    execute_process(
        COMMAND ${CPPLOAD_OPENSSL_EXECUTABLE} req -x509 -newkey rsa:2048
            -keyout "${CPPLOAD_TEST_CERT_DIR}/mismatch.key"
            -out "${CPPLOAD_TEST_CERT_DIR}/mismatch.pem"
            -days 3650 -nodes -subj "/CN=other.test"
            -addext "subjectAltName=DNS:other.test"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        RESULT_VARIABLE CPPLOAD_GEN_MISMATCH_RC
        OUTPUT_QUIET ERROR_QUIET)
endif()

add_executable(test_tls_integration test_tls_integration.cpp)
target_link_libraries(test_tls_integration
    PRIVATE cppload_core GTest::gtest_main
)
target_compile_definitions(test_tls_integration PRIVATE
    CPPLOAD_TEST_CERT_DIR="${CPPLOAD_TEST_CERT_DIR}")
if(CPPLOAD_OPENSSL_EXECUTABLE AND CPPLOAD_GEN_SERVER_RC STREQUAL "0" AND CPPLOAD_GEN_MISMATCH_RC STREQUAL "0")
    target_compile_definitions(test_tls_integration PRIVATE CPPLOAD_TEST_HAS_CERTS=1)
else()
    target_compile_definitions(test_tls_integration PRIVATE CPPLOAD_TEST_HAS_CERTS=0)
endif()

add_test(NAME HttpClientIntegrationTest COMMAND test_http_client_integration)
add_test(NAME VaultIntegrationTest COMMAND test_vault_integration)
add_test(NAME OAuth2IntegrationTest COMMAND test_oauth2_integration)
add_test(NAME ConnectionPoolIntegrationTest COMMAND test_connection_pool_integration)
add_test(NAME EngineIntegrationTest COMMAND test_engine_integration)
add_test(NAME TlsIntegrationTest COMMAND test_tls_integration)