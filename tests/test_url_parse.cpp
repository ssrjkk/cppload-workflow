#include <gtest/gtest.h>
#include "cppload/core/url_parse.hpp"

using namespace cppload::core;

TEST(UrlParseTest, BasicHttp) {
    auto p = parse_url("http://example.com/path");
    EXPECT_EQ(p.host, "example.com");
    EXPECT_EQ(p.port, "80");
    EXPECT_EQ(p.path, "/path");
    EXPECT_FALSE(p.tls);
}

TEST(UrlParseTest, BasicHttps) {
    auto p = parse_url("https://example.com/api/v1");
    EXPECT_EQ(p.host, "example.com");
    EXPECT_EQ(p.port, "443");
    EXPECT_EQ(p.path, "/api/v1");
    EXPECT_TRUE(p.tls);
}

TEST(UrlParseTest, ExplicitPort) {
    auto p = parse_url("http://localhost:8080/test");
    EXPECT_EQ(p.host, "localhost");
    EXPECT_EQ(p.port, "8080");
    EXPECT_EQ(p.path, "/test");
}

TEST(UrlParseTest, NoPath) {
    auto p = parse_url("http://example.com");
    EXPECT_EQ(p.host, "example.com");
    EXPECT_EQ(p.port, "80");
    EXPECT_EQ(p.path, "/");
}

TEST(UrlParseTest, EmptyUrl) {
    auto p = parse_url("");
    EXPECT_EQ(p.host, "");
    EXPECT_EQ(p.port, "80");
    EXPECT_EQ(p.path, "/");
    EXPECT_FALSE(p.tls);
}

TEST(UrlParseTest, NoProtocol) {
    auto p = parse_url("example.com/path");
    EXPECT_EQ(p.host, "example.com");
    EXPECT_EQ(p.port, "80");
    EXPECT_EQ(p.path, "/path");
}

TEST(UrlParseTest, DeepPath) {
    auto p = parse_url("https://api.example.com/v1/users/123/profile");
    EXPECT_EQ(p.host, "api.example.com");
    EXPECT_EQ(p.path, "/v1/users/123/profile");
    EXPECT_TRUE(p.tls);
}

TEST(UrlParseTest, PortOnly) {
    auto p = parse_url("http://host:9090");
    EXPECT_EQ(p.host, "host");
    EXPECT_EQ(p.port, "9090");
    EXPECT_EQ(p.path, "/");
}

TEST(SanitisePathTest, BasicPath) {
    EXPECT_EQ(sanitise_path("db/creds"), "db/creds");
}

TEST(SanitisePathTest, StripsSpecialChars) {
    EXPECT_EQ(sanitise_path("path with spaces"), "pathwithspaces");
    EXPECT_EQ(sanitise_path("path<script>"), "pathscript");
    EXPECT_EQ(sanitise_path("path;injection"), "pathinjection");
}

TEST(SanitisePathTest, PreservesSafeChars) {
    EXPECT_EQ(sanitise_path("a-b_c.d~e"), "a-b_c.d~e");
    EXPECT_EQ(sanitise_path("ABC/123"), "ABC/123");
}

TEST(SanitisePathTest, EmptyPath) {
    EXPECT_EQ(sanitise_path(""), "");
}
