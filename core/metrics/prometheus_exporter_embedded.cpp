// @author ssrjkk | cppload
#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/metrics/collector.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <sstream>
#include <cstdint>
#include <cstdlib>
#include <vector>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

#ifndef CPLOAD_HAVE_PROMETHEUS

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = boost::beast::http;

namespace cppload::metrics {

namespace {

struct BindAddress {
    std::string host{"0.0.0.0"};
    uint16_t port{9090};
};

BindAddress parse_bind_address(const std::string& address) {
    BindAddress result;
    auto colon = address.rfind(':');
    if (colon != std::string::npos) {
        result.host = address.substr(0, colon);
        try {
            auto p = std::stoul(address.substr(colon + 1));
            if (p > 0 && p <= 65535) result.port = static_cast<uint16_t>(p);
        } catch (const std::exception&) {
        }
    }
    if (result.host.empty()) result.host = "0.0.0.0";
    return result;
}

} // anonymous namespace

// Fallback exporter used when prometheus-cpp is not available at build time.
// Serves /metrics as Prometheus text exposition from a Boost.Beast HTTP server.
class PrometheusExporterImpl {
public:
    explicit PrometheusExporterImpl(const std::string& bind_address)
        : bind_(parse_bind_address(bind_address)) {}

    ~PrometheusExporterImpl() { stop(); }

    bool start() {
        if (running_) return true;
        try {
            acceptor_ = std::make_shared<asio::ip::tcp::acceptor>(ioc_);
            acceptor_->open(asio::ip::tcp::v4());
            acceptor_->set_option(asio::socket_base::reuse_address(true));
            acceptor_->bind(asio::ip::tcp::endpoint(
                asio::ip::make_address(bind_.host), bind_.port));
            acceptor_->listen(asio::socket_base::max_listen_connections);
            ioc_.restart();
            running_ = true;
            do_accept();
            thread_ = std::thread([this] { ioc_.run(); });
            return true;
        } catch (const std::exception&) {
            running_ = false;
            return false;
        }
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        boost::system::error_code ec;
        if (acceptor_) {
            acceptor_->cancel(ec);
            acceptor_->close(ec);
        }
        ioc_.stop();
        if (thread_.joinable()) thread_.join();

        // Join in-flight request handler threads so no handler outlives this
        // object (they capture `this`).
        std::vector<std::thread> handlers;
        {
            std::lock_guard<std::mutex> lock(threads_mtx_);
            handlers.swap(handler_threads_);
        }
        for (auto& t : handlers) {
            if (t.joinable()) t.join();
        }
    }

    bool is_running() const { return running_; }

    void update_metrics(const MetricsCollector& collector) {
        auto m = collector.snapshot();
        std::lock_guard<std::mutex> lock(metrics_mtx_);
        metrics_ = std::move(m);
        rps_ = collector.requests_per_second();
        err_rate_ = collector.error_rate();
    }

    std::string endpoint() const {
        return "http://" + bind_.host + ":" + std::to_string(bind_.port) + "/metrics";
    }

private:
    void do_accept() {
        if (!running_) return;
        auto socket = std::make_shared<asio::ip::tcp::socket>(ioc_);
        acceptor_->async_accept(*socket,
            [this, socket](const boost::system::error_code& ec) {
                if (!ec) {
                    std::thread t([this, socket]() { handle_request(socket); });
                    std::lock_guard<std::mutex> lock(threads_mtx_);
                    handler_threads_.push_back(std::move(t));
                }
                do_accept();
            });
    }

    void handle_request(std::shared_ptr<asio::ip::tcp::socket> socket) {
        try {
            // Bound reads so a stalled client cannot block the handler thread
            // forever (stop() joins these threads).
#if defined(_WIN32)
            unsigned long rcv_timeout_ms = 5000;
            ::setsockopt(socket->native_handle(), SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&rcv_timeout_ms),
                         static_cast<int>(sizeof(rcv_timeout_ms)));
#else
            struct timeval tv { 5, 0 };
            ::setsockopt(socket->native_handle(), SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&tv),
                         static_cast<socklen_t>(sizeof(tv)));
#endif
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            boost::system::error_code ec;
            http::read(*socket, buffer, req, ec);
            if (ec) return;

            http::response<http::string_body> res{http::status::ok, req.version()};
            res.set(http::field::server, "cppload-pro/1.0");
            res.set(http::field::content_type,
                "text/plain; version=0.0.4; charset=utf-8");
            if (req.method() == http::verb::get && req.target() == "/metrics") {
                res.body() = metrics_text();
            } else if (req.method() == http::verb::get && req.target() == "/") {
                res.body() = "cppload-pro prometheus exporter\n";
            } else {
                res.result(http::status::not_found);
                res.body() = "not found\n";
            }
            res.prepare_payload();
            http::write(*socket, res, ec);
            socket->shutdown(asio::ip::tcp::socket::shutdown_send, ec);
        } catch (const std::exception&) {
        }
    }

    std::string metrics_text() const {
        std::lock_guard<std::mutex> lock(metrics_mtx_);
        std::ostringstream oss;
        oss << "# HELP cppload_requests_total Total number of requests\n"
            << "# TYPE cppload_requests_total counter\n"
            << "cppload_requests_total " << metrics_.total_requests << "\n"
            << "# HELP cppload_requests_successful_total Total successful requests\n"
            << "# TYPE cppload_requests_successful_total counter\n"
            << "cppload_requests_successful_total " << metrics_.successful_requests << "\n"
            << "# HELP cppload_requests_failed_total Total failed requests\n"
            << "# TYPE cppload_requests_failed_total counter\n"
            << "cppload_requests_failed_total " << metrics_.failed_requests << "\n"
            << "# HELP cppload_error_rate Current error rate (percent)\n"
            << "# TYPE cppload_error_rate gauge\n"
            << "cppload_error_rate " << err_rate_ << "\n"
            << "# HELP cppload_requests_per_second Current requests per second\n"
            << "# TYPE cppload_requests_per_second gauge\n"
            << "cppload_requests_per_second " << rps_ << "\n"
            << "# HELP cppload_mean_latency_us Mean latency in microseconds\n"
            << "# TYPE cppload_mean_latency_us gauge\n"
            << "cppload_mean_latency_us " << metrics_.mean_latency_us << "\n"
            << "# HELP cppload_p95_latency_us P95 latency in microseconds\n"
            << "# TYPE cppload_p95_latency_us gauge\n"
            << "cppload_p95_latency_us " << metrics_.p95_latency_us << "\n"
            << "# HELP cppload_p99_latency_us P99 latency in microseconds\n"
            << "# TYPE cppload_p99_latency_us gauge\n"
            << "cppload_p99_latency_us " << metrics_.p99_latency_us << "\n";
        return oss.str();
    }

    BindAddress bind_;
    asio::io_context ioc_;
    std::shared_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::thread thread_;
    std::vector<std::thread> handler_threads_;
    mutable std::mutex threads_mtx_;
    std::atomic<bool> running_{false};
    mutable std::mutex metrics_mtx_;
    RequestMetrics metrics_{};
    double rps_{0.0};
    double err_rate_{0.0};
};

PrometheusExporter::PrometheusExporter(const std::string& bind_address)
    : impl_(std::make_unique<PrometheusExporterImpl>(bind_address)) {}

PrometheusExporter::~PrometheusExporter() = default;

bool PrometheusExporter::start() { return impl_->start(); }
void PrometheusExporter::stop() { impl_->stop(); }
bool PrometheusExporter::is_running() const { return impl_->is_running(); }
void PrometheusExporter::update_metrics(const MetricsCollector& collector) { impl_->update_metrics(collector); }
std::string PrometheusExporter::endpoint() const { return impl_->endpoint(); }

} // namespace cppload::metrics

#endif
