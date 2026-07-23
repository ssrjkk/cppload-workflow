#include <gtest/gtest.h>
#include "cppload/net/protocol_factory.hpp"
#include "cppload/security/tls_context.hpp"
#include <boost/asio/io_context.hpp>

TEST(ProtocolFactoryTest, CreateHttp11) {
    boost::asio::io_context ioc;
    ASSERT_NO_THROW({
        auto client = cppload::net::ProtocolFactory::create("http1.1", ioc);
        ASSERT_NE(client, nullptr);
        EXPECT_EQ(client->name(), "http1.1");
    });
}

TEST(ProtocolFactoryTest, CreateTcpRaw) {
    boost::asio::io_context ioc;
    ASSERT_NO_THROW({
        auto client = cppload::net::ProtocolFactory::create("tcp_raw", ioc);
        ASSERT_NE(client, nullptr);
        EXPECT_EQ(client->name(), "tcp_raw");
    });
}

TEST(ProtocolFactoryTest, CreateWebSocket) {
    boost::asio::io_context ioc;
    ASSERT_NO_THROW({
        auto client = cppload::net::ProtocolFactory::create("ws", ioc);
        ASSERT_NE(client, nullptr);
        EXPECT_EQ(client->name(), "ws");
    });
}

TEST(ProtocolFactoryTest, CreateUnknownThrows) {
    boost::asio::io_context ioc;
    EXPECT_THROW(
        cppload::net::ProtocolFactory::create("unknown", ioc),
        std::system_error
    );
}

TEST(ProtocolFactoryTest, CreateEmptyDefaultsHttp) {
    boost::asio::io_context ioc;
    auto client = cppload::net::ProtocolFactory::create("", ioc);
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(client->name(), "http1.1");
}

TEST(ProtocolFactoryTest, AvailableProtocols) {
    auto protocols = cppload::net::ProtocolFactory::available_protocols();
    EXPECT_GE(protocols.size(), 3u);
}

TEST(ProtocolFactoryTest, SetTlsConfig) {
    cppload::security::TlsConfig cfg;
    cfg.verify_peer = false;
    ASSERT_NO_THROW({
        cppload::net::ProtocolFactory::set_tls_config(cfg);
    });
}

TEST(ProtocolFactoryTest, RegisterCustomProtocol) {
    boost::asio::io_context ioc;
    cppload::net::ProtocolFactory::register_protocol("custom_test",
        [](boost::asio::io_context&,
          const cppload::security::TlsConfig&) -> std::unique_ptr<cppload::net::ProtocolClient> {
            return nullptr;
        });
    auto protocols = cppload::net::ProtocolFactory::available_protocols();
    bool found = false;
    for (const auto& p : protocols) {
        if (p == "custom_test") { found = true; break; }
    }
    EXPECT_TRUE(found);
}
