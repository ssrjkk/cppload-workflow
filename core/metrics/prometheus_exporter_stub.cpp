#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/metrics/collector.hpp"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>

#ifndef CPLOAD_HAVE_PROMETHEUS

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;

namespace cppload::metrics {

class PrometheusExporterImpl {
public:
    PrometheusExporterImpl(const std::string& bind_address)
        : bind_address_(bind_address)
        , acceptor_(ioc_)
        , running_(false)
    {
        parse_bind_address(bind_address_, listen_addr_, listen_port_);
    }

    ~PrometheusExporterImpl() { stop(); }

    bool start() {
        if (running_) return true;
        beast::error_code ec;
        auto addr = asio::ip::make_address(listen_addr_, ec);
        if (ec) return false;
        asio::ip::tcp::endpoint ep(addr, listen_port_);
        acceptor_.open(ep.protocol(), ec);
        if (ec) return false;
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec) return false;
        acceptor_.bind(ep, ec);
        if (ec) return false;
        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec) return false;
        running_ = true;
        thread_ = std::thread([this] { run(); });
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        beast::error_code ec;
        acceptor_.cancel(ec);
        acceptor_.close(ec);
        if (thread_.joinable()) thread_.join();
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
        return "http://" + bind_address_ + "/metrics";
    }

private:
    static void parse_bind_address(const std::string& address,
                                   std::string& out_addr, uint16_t& out_port) {
        auto colon = address.rfind(':');
        if (colon != std::string::npos) {
            out_addr = address.substr(0, colon);
            try {
                out_port = static_cast<uint16_t>(
                    std::stoul(address.substr(colon + 1)));
            } catch (...) {
                out_port = 9090;
            }
        } else {
            out_addr = address;
            out_port = 9090;
        }
        if (out_addr.empty() || out_addr == "0.0.0.0") {
            out_addr = "0.0.0.0";
        }
    }

    std::string format_metrics() {
        std::lock_guard<std::mutex> lock(metrics_mtx_);
        std::ostringstream os;
        os << "# HELP cppload_requests_total Total number of HTTP requests\n"
           << "# TYPE cppload_requests_total counter\n"
           << "cppload_requests_total " << metrics_.total_requests << "\n"
           << "# HELP cppload_requests_success_total Total successful requests\n"
           << "# TYPE cppload_requests_success_total counter\n"
           << "cppload_requests_success_total " << metrics_.successful_requests << "\n"
           << "# HELP cppload_requests_failed_total Total failed requests\n"
           << "# TYPE cppload_requests_failed_total counter\n"
           << "cppload_requests_failed_total " << metrics_.failed_requests << "\n"
           << "# HELP cppload_bytes_sent_total Total bytes sent\n"
           << "# TYPE cppload_bytes_sent_total counter\n"
           << "cppload_bytes_sent_total " << metrics_.total_bytes_sent << "\n"
           << "# HELP cppload_bytes_received_total Total bytes received\n"
           << "# TYPE cppload_bytes_received_total counter\n"
           << "cppload_bytes_received_total " << metrics_.total_bytes_received << "\n"
           << "# HELP cppload_requests_per_second Current requests per second\n"
           << "# TYPE cppload_requests_per_second gauge\n"
           << "cppload_requests_per_second " << rps_ << "\n"
           << "# HELP cppload_error_rate_percent Current error rate\n"
           << "# TYPE cppload_error_rate_percent gauge\n"
           << "cppload_error_rate_percent " << err_rate_ << "\n";
        if (metrics_.total_requests > 0) {
            os << "# HELP cppload_latency_seconds Request latency\n"
               << "# TYPE cppload_latency_seconds gauge\n"
               << "cppload_latency_mean_seconds " << (metrics_.mean_latency_us / 1e6) << "\n"
               << "cppload_latency_p95_seconds " << (metrics_.p95_latency_us / 1e6) << "\n"
               << "cppload_latency_p99_seconds " << (metrics_.p99_latency_us / 1e6) << "\n";
        }
        return os.str();
    }

    void run() {
        while (running_) {
            beast::error_code ec;
            asio::ip::tcp::socket socket(ioc_);
            acceptor_.accept(socket, ec);
            if (ec || !running_) break;
            handle_request(std::move(socket));
        }
    }

    void handle_request(asio::ip::tcp::socket socket) {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        beast::error_code ec;
        http::read(socket, buffer, req, ec);
        if (ec && ec != http::error::end_of_stream) return;

        std::string body;
        unsigned status = 200;
        std::string content_type = "text/plain; version=0.0.4";
        if (req.method() == http::verb::get && req.target() == "/metrics") {
            body = format_metrics();
        } else {
            status = 404;
            body = "Not Found\n";
        }
        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::server, "cppload-pro/1.0");
        res.set(http::field::content_type, content_type);
        res.body() = body;
        res.prepare_payload();
        res.keep_alive(false);
        http::write(socket, res, ec);
        beast::error_code shutdown_ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_send, shutdown_ec);
    }

    std::string bind_address_;
    std::string listen_addr_;
    uint16_t listen_port_{9090};
    asio::io_context ioc_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;
    std::atomic<bool> running_;
    mutable std::mutex metrics_mtx_;
    RequestMetrics metrics_;
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
