// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/security/tls_context.hpp"
#include <boost/system/system_error.hpp>
#include <boost/asio/ssl/error.hpp>

using namespace cppload::security;

TEST(TlsContextTest, DefaultConfig) {
    ASSERT_NO_THROW({
        TlsContext ctx;
        EXPECT_FALSE(ctx.is_mtls_enabled());
    });
}

TEST(TlsContextTest, Tls12Config) {
    TlsConfig cfg;
    cfg.verify_peer = false;
    cfg.min_tls_version = TlsVersion::TLS_1_2;
    ASSERT_NO_THROW({
        TlsContext ctx(cfg);
        EXPECT_FALSE(ctx.is_mtls_enabled());
    });
}

TEST(TlsContextTest, Tls13Config) {
    TlsConfig cfg;
    cfg.verify_peer = false;
    cfg.min_tls_version = TlsVersion::TLS_1_3;
    ASSERT_NO_THROW({
        TlsContext ctx(cfg);
        EXPECT_FALSE(ctx.is_mtls_enabled());
    });
}

TEST(TlsContextTest, MtlsRequiresCertAndKey) {
    TlsConfig cfg;
    cfg.use_mtls = true;
    cfg.cert_chain_file = "";
    cfg.private_key_file = "";
    EXPECT_THROW(TlsContext ctx(cfg), std::invalid_argument);
}

TEST(TlsContextTest, MtlsWithCertAndKey) {
    TlsConfig cfg;
    cfg.use_mtls = true;
    cfg.verify_peer = false;
    cfg.cert_chain_file = "/nonexistent.pem";
    cfg.private_key_file = "/nonexistent.key";
    try {
        TlsContext ctx(cfg);
        FAIL() << "expected boost::system::system_error for missing cert file";
    } catch (const boost::system::system_error& e) {
        // The cause must be the missing file (ENOENT), not an arbitrary code.
        EXPECT_EQ(e.code(), std::errc::no_such_file_or_directory)
            << "expected ENOENT, got: " << e.code().message();
        EXPECT_FALSE(e.code().message().empty());
    }
}

TEST(TlsContextTest, TlsVersionEnumValues) {
    EXPECT_EQ(static_cast<int>(TlsVersion::TLS_1_2), 12);
    EXPECT_EQ(static_cast<int>(TlsVersion::TLS_1_3), 13);
}

TEST(TlsContextTest, GetNativeContext) {
    TlsContext ctx;
    auto& native = ctx.get_native_context();
    (void)native;
    SUCCEED();
}
