add_executable(test_http_client_integration test_http_client_integration.cpp)
target_link_libraries(test_http_client_integration
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_vault_integration test_vault_integration.cpp)
target_link_libraries(test_vault_integration
    PRIVATE cppload_core GTest::gtest_main
)

add_executable(test_oauth2_integration test_oauth2_integration.cpp)
target_link_libraries(test_oauth2_integration
    PRIVATE cppload_core GTest::gtest_main
)

add_test(NAME HttpClientIntegrationTest COMMAND test_http_client_integration)
add_test(NAME VaultIntegrationTest COMMAND test_vault_integration)
add_test(NAME OAuth2IntegrationTest COMMAND test_oauth2_integration)
