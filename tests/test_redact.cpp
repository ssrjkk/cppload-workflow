// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/core/redact.hpp"

using namespace cppload::core;

TEST(Redact, HidesSecretPatterns) {
    const std::string input =
        "Authorization: Bearer abc123def456\r\n"
        "X-API-Key: supersecretkey\r\n"
        "client_secret: myclientsecret\r\n"
        "access_token: tok_access\r\n"
        "refresh_token: tok_refresh\r\n"
        "vault_token: tok_vault\r\n"
        "password: hunter2\r\n"
        "private_key: -----BEGIN PRIVATE KEY-----\r\n";

    std::string out = redact::redact_secrets(input);
    for (const auto& secret :
         {"abc123def456", "supersecretkey", "myclientsecret",
          "tok_access", "tok_refresh", "tok_vault", "hunter2",
          "-----BEGIN PRIVATE KEY-----"}) {
        EXPECT_EQ(out.find(secret), std::string::npos)
            << "secret leaked: " << secret;
    }
}

TEST(Redact, JsonFormsMasked) {
    const std::string input =
        R"({"client_secret":"jsonsecret","access_token": "jsontoken","token":"jsontok2"})";
    std::string out = redact::redact_secrets(input);
    EXPECT_EQ(out.find("jsonsecret"), std::string::npos);
    EXPECT_EQ(out.find("jsontoken"), std::string::npos);
    EXPECT_EQ(out.find("jsontok2"), std::string::npos);
    EXPECT_NE(out.find("******"), std::string::npos);
}

TEST(Redact, CaseInsensitiveKeys) {
    const std::string input = "AUTHORIZATION: Bearer UppercaseSecret\n"
                              "Client_Secret=CaseSecret\n"
                              "x-API-key: casekey\n";
    std::string out = redact::redact_secrets(input);
    for (const auto& s : {"UppercaseSecret", "CaseSecret", "casekey"}) {
        EXPECT_EQ(out.find(s), std::string::npos) << "leaked: " << s;
    }
}

TEST(Redact, FormEncodedMasked) {
    const std::string input = "client_secret=formsecret&grant_type=client_credentials&token=formtoken";
    std::string out = redact::redact_secrets(input);
    EXPECT_EQ(out.find("formsecret"), std::string::npos);
    EXPECT_EQ(out.find("formtoken"), std::string::npos);
    EXPECT_NE(out.find("client_credentials"), std::string::npos);
}

TEST(Redact, NonSecretContentUntouched) {
    const std::string input = "Authorization: Bearer x\r\n"
                              "Content-Type: application/json\r\n"
                              "Host: example.com\r\n"
                              "body: plain text here\r\n";
    std::string out = redact::redact_secrets(input);
    EXPECT_NE(out.find("application/json"), std::string::npos);
    EXPECT_NE(out.find("example.com"), std::string::npos);
    EXPECT_NE(out.find("plain text here"), std::string::npos);
}

TEST(Redact, EmptyInput) {
    EXPECT_EQ(redact::redact_secrets(""), "");
}

TEST(Redact, SafeLogWrapper) {
    std::string out = redact::safe_log("token=lolsecret endpoint=http://h/x");
    EXPECT_EQ(out.find("lolsecret"), std::string::npos);
}