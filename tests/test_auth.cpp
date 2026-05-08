#include <gtest/gtest.h>
#include "cppload/security/auth_provider.hpp"
#include <unordered_map>

TEST(AuthProviderTest, NoAuth) {
    cppload::security::AuthProvider auth;
    std::unordered_map<std::string, std::string> headers;
    auth.apply_headers(headers);
    EXPECT_TRUE(headers.empty());
}

TEST(AuthProviderTest, BearerToken) {
    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::BEARER_TOKEN;
    cfg.token = "test-token-123";
    cppload::security::AuthProvider auth(cfg);

    std::unordered_map<std::string, std::string> headers;
    auth.apply_headers(headers);
    EXPECT_EQ(headers["Authorization"], "Bearer test-token-123");
}

TEST(AuthProviderTest, ApiKey) {
    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::API_KEY;
    cfg.api_key = "key-456";
    cppload::security::AuthProvider auth(cfg);

    std::unordered_map<std::string, std::string> headers;
    auth.apply_headers(headers);
    EXPECT_EQ(headers["X-API-Key"], "key-456");
}

TEST(AuthProviderTest, IsExpiredNone) {
    cppload::security::AuthProvider auth;
    EXPECT_FALSE(auth.is_expired());
}

TEST(AuthProviderTest, RefreshNonOAuth) {
    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::BEARER_TOKEN;
    cfg.token = "static";
    cppload::security::AuthProvider auth(cfg);
    EXPECT_TRUE(auth.refresh_token()); // Non-OAuth always succeeds
}
