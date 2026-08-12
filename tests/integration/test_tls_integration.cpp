// @author ssrjkk | cppload
// End-to-end TLS tests exercising real handshakes against a local TLS server:
// hostname verification (match/mismatch/verify-disabled), SNI-skip for IP
// literals across the HTTP, raw-TCP and WebSocket clients, and the connection
// pool's SslConnector path.
#include <gtest/gtest.h>
#include "cppload/net/http_client.hpp"
#include "cppload/net/tcp_raw_client.hpp"
#include "cppload/net/ws_client.hpp"
#include "cppload/net/connection.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <atomic>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;

namespace {

#if CPPLOAD_TEST_HAS_CERTS == 0
bool has_certs() { return false; }
#else
bool has_certs() {
    std::string dir = CPPLOAD_TEST_CERT_DIR;
    if (dir.empty()) return false;
    return std::ifstream(dir + "/server.pem").good()
        && std::ifstream(dir + "/server.key").good()
        && std::ifstream(dir + "/mismatch.pem").good()
        && std::ifstream(dir + "/mismatch.key").good();
}
#endif

std::string cert_path(const std::string& name) {
    return std::string(CPPLOAD_TEST_CERT_DIR) + "/" + name;
}

// Minimal single-connection-per-accept TLS server that hands a fixed reply to
// whatever the client sends. Enough to complete a client->server round trip.
class TlsTestServer {
public:
    enum class Mode { kHttp, kRaw, kWs };

    TlsTestServer()
        : acceptor_(ioc_)
        , ctx_(boost::asio::ssl::context::tls_server)
        , running_(false)
    {
    }

    ~TlsTestServer() { stop(); }

    TlsTestServer(const TlsTestServer&) = delete;
    TlsTestServer& operator=(const TlsTestServer&) = delete;

    bool start(const std::string& cert, const std::string& key, Mode mode) {
        try {
            ctx_.set_options(boost::asio::ssl::context::default_workarounds);
            ctx_.use_certificate_chain_file(cert);
            ctx_.use_private_key_file(key, boost::asio::ssl::context::pem);
            ctx_.set_verify_mode(boost::asio::ssl::verify_none);
            auto ep = asio::ip::tcp::endpoint(
                asio::ip::make_address("127.0.0.1"), 0);
            acceptor_.open(ep.protocol());
            acceptor_.set_option(asio::socket_base::reuse_address(true));
            acceptor_.bind(ep);
            acceptor_.listen();
            port_ = acceptor_.local_endpoint().port();
            mode_ = mode;
            running_ = true;
            accept_thread_ = std::thread([this]() { accept_loop(); });
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    uint16_t port() const { return port_; }

    void stop() {
        running_ = false;
        beast::error_code ec;
        acceptor_.close(ec);
        if (accept_thread_.joinable()) accept_thread_.join();
        std::lock_guard<std::mutex> lock(sessions_mtx_);
        for (auto& t : sessions_) {
            if (t.joinable()) t.join();
        }
        sessions_.clear();
    }

private:
    // Non-blocking accept loop so stop() can interrupt it: closing a listener
    // does NOT wake a thread blocked in a blocking accept() on Linux, which
    // deadlocks ~TlsTestServer(). A non-blocking acceptor returns would_block
    // immediately, so the loop wakes up every few ms and exits as soon as
    // running_ flips.
    void accept_loop() {
        beast::error_code ec;
        acceptor_.non_blocking(true, ec);
        while (running_) {
            asio::ip::tcp::socket socket(ioc_);
            ec.clear();
            acceptor_.accept(socket, ec);
            if (ec == asio::error::would_block ||
                ec == asio::error::try_again) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            if (ec || !running_) break;
            socket.non_blocking(false, ec);
            {
                std::lock_guard<std::mutex> lock(sessions_mtx_);
                sessions_.emplace_back(
                    [this, s = std::move(socket)]() mutable {
                        handle(std::move(s));
                    });
            }
        }
    }

    void handle(asio::ip::tcp::socket socket) {
        try {
            if (mode_ == Mode::kWs) {
                handle_ws(std::move(socket));
                return;
            }

            asio::ssl::stream<asio::ip::tcp::socket> ssl(
                std::move(socket), ctx_);
            beast::error_code ec;
            ssl.handshake(asio::ssl::stream_base::server, ec);
            if (ec) return;

            beast::flat_buffer buf;
            boost::asio::read(ssl, buf, boost::asio::transfer_at_least(1), ec);
            if (ec) return;

            if (mode_ == Mode::kHttp) {
                std::string resp =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Length: 2\r\n"
                    "Content-Type: text/plain\r\n"
                    "Connection: close\r\n"
                    "\r\nOK";
                boost::asio::write(ssl, boost::asio::buffer(resp), ec);
            } else { // kRaw
                boost::asio::write(ssl, boost::asio::buffer(std::string("PONG")), ec);
            }

            beast::error_code shut;
            ssl.shutdown(shut);
        } catch (const std::exception&) {
        }
    }

    void handle_ws(asio::ip::tcp::socket socket) {
        beast::tcp_stream tcp(std::move(socket));
        asio::ssl::stream<beast::tcp_stream> ssl(std::move(tcp), ctx_);
        beast::error_code ec;
        ssl.handshake(asio::ssl::stream_base::server, ec);
        if (ec) return;
        websocket::stream<asio::ssl::stream<beast::tcp_stream>> ws(
            std::move(ssl));
        ws.accept(ec);
        if (ec) return;
        beast::flat_buffer buf;
        ws.read(buf, ec);
        if (ec) return;
        ws.write(boost::asio::buffer(std::string("pong")), ec);
        beast::error_code close_ec;
        ws.close(websocket::close_code::normal, close_ec);
    }

    asio::io_context ioc_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ssl::context ctx_;
    std::thread accept_thread_;
    std::vector<std::thread> sessions_;
    std::mutex sessions_mtx_;
    std::atomic<bool> running_;
    Mode mode_{Mode::kHttp};
    uint16_t port_{0};
};

class TlsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_certs()) {
            GTEST_SKIP() << "TLS test certificates not generated "
                            "(openssl missing at configure time)";
        }
    }
};

cppload::security::TlsConfig trusted_ca_config(const std::string& ca) {
    cppload::security::TlsConfig tls;
    tls.verify_peer = true;
    tls.ca_cert_file = ca;
    return tls;
}

} // namespace

TEST_F(TlsIntegrationTest, HttpHostnameMatchLocalhost) {
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("server.pem"), cert_path("server.key"),
                             TlsTestServer::Mode::kHttp));

    boost::asio::io_context ioc;
    cppload::net::Http11Client client(
        ioc, trusted_ca_config(cert_path("server.pem")));
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "localhost";
    req.port = server.port();
    req.use_tls = true;

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 200);
        called = true;
    });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST_F(TlsIntegrationTest, HttpIpLiteralMatch) {
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("server.pem"), cert_path("server.key"),
                             TlsTestServer::Mode::kHttp));

    boost::asio::io_context ioc;
    cppload::net::Http11Client client(
        ioc, trusted_ca_config(cert_path("server.pem")));
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "127.0.0.1";
    req.port = server.port();
    req.use_tls = true;

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 200);
        called = true;
    });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST_F(TlsIntegrationTest, HttpHostnameMismatchFails) {
    // The cert covers only DNS:other.test. Chain validation passes (ca is the
    // cert itself) but hostname verification for "localhost" must abort the
    // handshake.
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("mismatch.pem"), cert_path("mismatch.key"),
                             TlsTestServer::Mode::kHttp));

    boost::asio::io_context ioc;
    cppload::net::Http11Client client(
        ioc, trusted_ca_config(cert_path("mismatch.pem")));
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "localhost";
    req.port = server.port();
    req.use_tls = true;

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response) {
        EXPECT_EQ(ec, cppload::Err::tls_handshake_failed)
            << "Expected hostname mismatch to fail the handshake, got: "
            << ec.message();
        called = true;
    });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST_F(TlsIntegrationTest, HttpVerifyDisabledAllowsMismatch) {
    // With verify_peer=false the hostname callback must not run: the handshake
    // succeeds even though the cert does not cover the requested host.
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("mismatch.pem"), cert_path("mismatch.key"),
                             TlsTestServer::Mode::kHttp));

    cppload::security::TlsConfig tls;
    tls.verify_peer = false;

    boost::asio::io_context ioc;
    cppload::net::Http11Client client(ioc, tls);
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "localhost";
    req.port = server.port();
    req.use_tls = true;

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 200);
        called = true;
    });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST_F(TlsIntegrationTest, TcpRawIpLiteral) {
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("server.pem"), cert_path("server.key"),
                             TlsTestServer::Mode::kRaw));

    boost::asio::io_context ioc;
    cppload::net::TcpRawClient client(
        ioc, trusted_ca_config(cert_path("server.pem")));
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/";
    req.host = "127.0.0.1";
    req.port = server.port();
    req.use_tls = true;
    req.body = "ping";

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 1);
        EXPECT_EQ(resp.body, "PONG");
        called = true;
    });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST_F(TlsIntegrationTest, SslConnectorIpLiteral) {
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("server.pem"), cert_path("server.key"),
                             TlsTestServer::Mode::kRaw));

    boost::asio::io_context ioc;
    boost::asio::ssl::context client_ctx(
        boost::asio::ssl::context::tlsv12_client);
    client_ctx.set_verify_mode(boost::asio::ssl::verify_peer);
    client_ctx.load_verify_file(cert_path("server.pem"));

    cppload::net::SslConnector connector(ioc, client_ctx);
    connector.set_timeout(std::chrono::seconds(5));

    std::atomic<bool> called{false};
    connector.async_connect(
        "127.0.0.1", server.port(),
        [&](std::error_code ec, std::unique_ptr<cppload::net::Connection> conn) {
            ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
            ASSERT_TRUE(conn);
            EXPECT_TRUE(conn->is_open());
            called = true;
        });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}

TEST_F(TlsIntegrationTest, WsIpLiteral) {
    TlsTestServer server;
    ASSERT_TRUE(server.start(cert_path("server.pem"), cert_path("server.key"),
                             TlsTestServer::Mode::kWs));

    boost::asio::io_context ioc;
    cppload::net::WsClient client(
        ioc, trusted_ca_config(cert_path("server.pem")));
    client.set_timeout(std::chrono::seconds(5));

    cppload::net::Request req;
    req.method = "GET";
    req.path = "/ws";
    req.host = "127.0.0.1";
    req.port = server.port();
    req.use_tls = true;
    req.body = "hello";

    std::atomic<bool> called{false};
    client.async_request(req, [&](std::error_code ec, cppload::net::Response resp) {
        ASSERT_FALSE(ec) << "Unexpected error: " << ec.message();
        EXPECT_EQ(resp.status_code, 1);
        EXPECT_EQ(resp.body, "pong");
        called = true;
    });
    ioc.run_for(std::chrono::seconds(10));
    EXPECT_TRUE(called);
}
