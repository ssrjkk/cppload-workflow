#include <gtest/gtest.h>
#include "cppload/security/auth_provider.hpp"
#include "mock_server.hpp"
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

TEST(OAuth2IntegrationTest, GetBearerToken) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target().find("/oauth/token") == std::string::npos) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }

        json resp_body;
        resp_body["access_token"] = "s.oauth-token-abc";
        resp_body["expires_in"] = 3600;
        resp_body["token_type"] = "Bearer";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = resp_body.dump();
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::OAUTH2;
    cfg.client_id = "my-client";
    cfg.client_secret = "my-secret";
    cfg.token_endpoint = "http://127.0.0.1:" + std::to_string(server.port()) + "/oauth/token";

    cppload::security::AuthProvider auth(cfg);
    EXPECT_FALSE(auth.is_expired());
    EXPECT_FALSE(auth.get_auth_header().empty());

    std::unordered_map<std::string, std::string> headers;
    auth.apply_headers(headers);
    EXPECT_EQ(headers["Authorization"], "Bearer s.oauth-token-abc");
}

TEST(OAuth2IntegrationTest, AutoRefreshOnExpiry) {
    MockHttpServer server;
    int call_count = 0;
    server.set_handler([&call_count](const auto& req) {
        call_count++;
        json resp_body;
        resp_body["access_token"] = "s.token-" + std::to_string(call_count);
        resp_body["expires_in"] = 1;
        resp_body["token_type"] = "Bearer";

        http::response<http::string_body> res;
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = resp_body.dump();
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::OAUTH2;
    cfg.client_id = "my-client";
    cfg.client_secret = "my-secret";
    cfg.token_endpoint = "http://127.0.0.1:" + std::to_string(server.port()) + "/oauth/token";

    cppload::security::AuthProvider auth(cfg);

    std::unordered_map<std::string, std::string> headers;
    auth.apply_headers(headers);
    EXPECT_EQ(headers["Authorization"], "Bearer s.token-1");

    std::this_thread::sleep_for(std::chrono::seconds(2));

    EXPECT_TRUE(auth.is_expired());
    EXPECT_TRUE(auth.refresh_token());

    std::unordered_map<std::string, std::string> headers2;
    auth.apply_headers(headers2);
    EXPECT_EQ(headers2["Authorization"], "Bearer s.token-2");
    EXPECT_GE(call_count, 2);
}

TEST(OAuth2IntegrationTest, ServerError) {
    MockHttpServer server;
    server.set_handler([](const auto&) {
        http::response<http::string_body> res;
        res.result(http::status::bad_request);
        res.body() = R"({"error":"invalid_client"})";
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::OAUTH2;
    cfg.client_id = "bad-client";
    cfg.client_secret = "bad-secret";
    cfg.token_endpoint = "http://127.0.0.1:" + std::to_string(server.port()) + "/oauth/token";

    EXPECT_THROW(
        cppload::security::AuthProvider auth(cfg),
        std::runtime_error
    );
}

TEST(OAuth2IntegrationTest, BearerTokenStatic) {
    cppload::security::AuthConfig cfg;
    cfg.type = cppload::security::AuthType::BEARER_TOKEN;
    cfg.token = "static-token";

    cppload::security::AuthProvider auth(cfg);

    std::unordered_map<std::string, std::string> headers;
    auth.apply_headers(headers);
    EXPECT_EQ(headers["Authorization"], "Bearer static-token");
    EXPECT_FALSE(auth.is_expired());
    EXPECT_TRUE(auth.refresh_token());
}
