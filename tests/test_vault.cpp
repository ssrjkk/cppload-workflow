// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/vault/vault_client.hpp"

static const char* VAULT_TEST_ADDR = "http://192.0.2.1:8200";

TEST(VaultClientTest, ConstructDefault) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    EXPECT_NO_THROW(client.is_connected());
}

TEST(VaultClientTest, GetSecretNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_secret("test-path", "test-key");
    ASSERT_FALSE(result);
    EXPECT_NE(result.error(), cppload::Err::success);
}

TEST(VaultClientTest, GetSecretMapNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_secret_map("test-path");
    ASSERT_FALSE(result);
    EXPECT_NE(result.error(), cppload::Err::success);
}

TEST(VaultClientTest, PutSecretNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    std::unordered_map<std::string, std::string> data = {{"key", "value"}};
    auto result = client.put_secret("test", data);
    ASSERT_FALSE(result);
    EXPECT_NE(result.error(), cppload::Err::success);
}

TEST(VaultClientTest, GetDatabaseCredsNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_database_creds("test-role");
    ASSERT_FALSE(result);
    EXPECT_NE(result.error(), cppload::Err::success);
}

TEST(VaultClientTest, GetAppRoleTokenNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_approle_token("role-id", "secret-id");
    ASSERT_FALSE(result);
    EXPECT_NE(result.error(), cppload::Err::success);
}

TEST(VaultClientTest, GetSecretEmptyPath) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_secret("", "key");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), cppload::Err::invalid_config);
}

TEST(VaultClientTest, GetSecretEmptyRole) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_database_creds("");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), cppload::Err::invalid_config);
}

TEST(VaultClientTest, GetAppRoleTokenEmptyArgs) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto result = client.get_approle_token("", "");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), cppload::Err::invalid_config);
}