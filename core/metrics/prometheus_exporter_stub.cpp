// @author ssrjkk | cppload
#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/metrics/collector.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <cstdint>

#ifndef CPLOAD_HAVE_PROMETHEUS

namespace cppload::metrics {

class PrometheusExporterImpl {
public:
    explicit PrometheusExporterImpl(const std::string& bind_address)
        : bind_address_(bind_address) {}

    ~PrometheusExporterImpl() { stop(); }

    bool start() {
        if (running_) return true;
        // Real HTTP server requires prometheus-cpp (full impl) or Boost.Beast embedded.
        // On systems without prometheus-cpp, start() succeeds but does not bind.
        running_ = true;
        return true;
    }

    void stop() {
        if (!running_) return;
        running_ = false;
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
    std::string bind_address_;
    std::thread thread_;
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