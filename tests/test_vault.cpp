#include <gtest/gtest.h>
#include "cppload/vault/vault_client.hpp"

// Deterministic: uses unroutable address so connection always fails
static const char* VAULT_TEST_ADDR = "http://192.0.2.1:8200";

TEST(VaultClientTest, ConstructDefault) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    // Should not crash, connection will fail (no route to 192.0.2.1)
    EXPECT_NO_THROW(client.is_connected());
}

TEST(VaultClientTest, GetSecretNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto secret = client.get_secret("test-path", "test-key");
    // Should not crash, returns empty
    EXPECT_TRUE(secret.empty());
}

TEST(VaultClientTest, GetSecretMapNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto map = client.get_secret_map("test-path");
    EXPECT_TRUE(map.empty());
}

TEST(VaultClientTest, PutSecretNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    std::unordered_map<std::string, std::string> data = {{"key", "value"}};
    EXPECT_FALSE(client.put_secret("test", data));
}

TEST(VaultClientTest, GetDatabaseCredsNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto creds = client.get_database_creds("test-role");
    EXPECT_TRUE(creds.empty());
}

TEST(VaultClientTest, GetAppRoleTokenNoServer) {
    cppload::vault::VaultConfig cfg;
    cfg.address = VAULT_TEST_ADDR;
    cfg.token = "test";
    cfg.timeout_seconds = 1;
    cppload::vault::VaultClient client(cfg);
    auto token = client.get_approle_token("role-id", "secret-id");
    EXPECT_TRUE(token.empty());
}
