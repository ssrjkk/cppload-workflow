#include <gtest/gtest.h>
#include "cppload/vault/vault_client.hpp"
#include "mock_server.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

TEST(VaultIntegrationTest, GetSecret) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target().find("/v1/secret/data/") == std::string::npos) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }

        json resp_body;
        resp_body["data"]["data"]["password"] = "my-secret-pass";
        resp_body["data"]["data"]["username"] = "admin";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = resp_body.dump();
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::vault::VaultConfig cfg;
    cfg.address = "http://127.0.0.1:" + std::to_string(server.port());
    cfg.token = "test-token";
    cfg.timeout_seconds = 5;

    cppload::vault::VaultClient client(cfg);
    ASSERT_TRUE(client.is_connected());

    auto password = client.get_secret("db/creds", "password");
    EXPECT_EQ(password, "my-secret-pass");

    auto username = client.get_kv_secret("db/creds", "username");
    EXPECT_EQ(username, "admin");
}

TEST(VaultIntegrationTest, GetSecretMap) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target().find("/v1/secret/data/") == std::string::npos) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }

        json resp_body;
        resp_body["data"]["data"]["key1"] = "val1";
        resp_body["data"]["data"]["key2"] = "val2";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = resp_body.dump();
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::vault::VaultConfig cfg;
    cfg.address = "http://127.0.0.1:" + std::to_string(server.port());
    cfg.token = "test";
    cfg.timeout_seconds = 5;

    cppload::vault::VaultClient client(cfg);
    auto map = client.get_secret_map("myapp/config");
    ASSERT_EQ(map.size(), 2);
    EXPECT_EQ(map["key1"], "val1");
    EXPECT_EQ(map["key2"], "val2");
}

TEST(VaultIntegrationTest, PutSecret) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target().find("/v1/secret/data/") == std::string::npos) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }
        if (req.method() != http::verb::post) {
            res.result(http::status::method_not_allowed);
            res.prepare_payload();
            return res;
        }

        res.result(http::status::ok);
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::vault::VaultConfig cfg;
    cfg.address = "http://127.0.0.1:" + std::to_string(server.port());
    cfg.token = "test";
    cfg.timeout_seconds = 5;

    cppload::vault::VaultClient client(cfg);
    std::unordered_map<std::string, std::string> data = {{"apikey", "abc123"}};
    EXPECT_TRUE(client.put_secret("api/keys", data));
}

TEST(VaultIntegrationTest, GetAppRoleToken) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target().find("/v1/auth/approle/login") == std::string::npos) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }

        json resp_body;
        resp_body["auth"]["client_token"] = "s.approle-token-789";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = resp_body.dump();
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::vault::VaultConfig cfg;
    cfg.address = "http://127.0.0.1:" + std::to_string(server.port());
    cfg.token = "test";
    cfg.timeout_seconds = 5;

    cppload::vault::VaultClient client(cfg);
    auto token = client.get_approle_token("role-123", "secret-456");
    EXPECT_EQ(token, "s.approle-token-789");
}

TEST(VaultIntegrationTest, GetDatabaseCreds) {
    MockHttpServer server;
    server.set_handler([](const auto& req) {
        http::response<http::string_body> res;
        if (req.target().find("/v1/database/creds/") == std::string::npos) {
            res.result(http::status::not_found);
            res.prepare_payload();
            return res;
        }

        json resp_body;
        resp_body["data"]["username"] = "vault-user-db1";
        resp_body["data"]["password"] = "vault-pass-db1";

        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = resp_body.dump();
        res.prepare_payload();
        return res;
    });
    ASSERT_TRUE(server.start());

    cppload::vault::VaultConfig cfg;
    cfg.address = "http://127.0.0.1:" + std::to_string(server.port());
    cfg.token = "test";
    cfg.timeout_seconds = 5;

    cppload::vault::VaultClient client(cfg);
    auto creds = client.get_database_creds("my-db-role");
    EXPECT_EQ(creds, "vault-user-db1:vault-pass-db1");
}
