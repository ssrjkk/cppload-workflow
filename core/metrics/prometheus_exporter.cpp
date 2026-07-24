// @author ssrjkk | cppload
#include "cppload/metrics/prometheus_exporter.hpp"

#ifdef CPLOAD_HAVE_PROMETHEUS

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <memory>
#include <chrono>
#include <mutex>

namespace cppload::metrics {

class PrometheusExporterImpl {
public:
    explicit PrometheusExporterImpl(const std::string& bind_address)
        : bind_address_(bind_address)
    {
    }

    bool start() {
        std::lock_guard<std::mutex> lock(mtx_);
        exposer_ = std::make_unique<prometheus::Exposer>(bind_address_);
        registry_ = std::make_shared<prometheus::Registry>();
        exposer_->RegisterCollectable(registry_);

        // Create metrics
        auto& total_requests = prometheus::BuildCounter()
            .Name("cppload_requests_total")
            .Help("Total number of HTTP requests")
            .Register(*registry_);
        total_requests_ = &total_requests.Add({});

        auto& successful_requests = prometheus::BuildCounter()
            .Name("cppload_requests_success_total")
            .Help("Total number of successful requests")
            .Register(*registry_);
        successful_requests_ = &successful_requests.Add({});

        auto& failed_requests = prometheus::BuildCounter()
            .Name("cppload_requests_failed_total")
            .Help("Total number of failed requests")
            .Register(*registry_);
        failed_requests_ = &failed_requests.Add({});

        auto& bytes_sent = prometheus::BuildCounter()
            .Name("cppload_bytes_sent_total")
            .Help("Total bytes sent")
            .Register(*registry_);
        bytes_sent_ = &bytes_sent.Add({});

        auto& bytes_received = prometheus::BuildCounter()
            .Name("cppload_bytes_received_total")
            .Help("Total bytes received")
            .Register(*registry_);
        bytes_received_ = &bytes_received.Add({});

        auto& latency_histogram = prometheus::BuildHistogram()
            .Name("cppload_request_duration_seconds")
            .Help("Request latency in seconds")
            .Buckets({0.001, 0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0})
            .Register(*registry_);
        latency_histogram_ = &latency_histogram.Add({});

        auto& rps_gauge = prometheus::BuildGauge()
            .Name("cppload_requests_per_second")
            .Help("Current requests per second")
            .Register(*registry_);
        rps_gauge_ = &rps_gauge.Add({});

        auto& error_rate_gauge = prometheus::BuildGauge()
            .Name("cppload_error_rate_percent")
            .Help("Current error rate in percent")
            .Register(*registry_);
        error_rate_gauge_ = &error_rate_gauge.Add({});
        return true;
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mtx_);
        total_requests_ = nullptr;
        successful_requests_ = nullptr;
        failed_requests_ = nullptr;
        bytes_sent_ = nullptr;
        bytes_received_ = nullptr;
        latency_histogram_ = nullptr;
        rps_gauge_ = nullptr;
        error_rate_gauge_ = nullptr;
        exposer_.reset();
        registry_.reset();
    }

    bool is_running() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return exposer_ != nullptr;
    }
    
    void update_metrics(const MetricsCollector& collector) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (!total_requests_) return;
        auto metrics = collector.snapshot();

        auto delta_or_reset = [](uint64_t curr, uint64_t& last) -> double {
            if (curr >= last) {
                double d = static_cast<double>(curr - last);
                last = curr;
                return d;
            }
            // Counter reset detected — use current value as increment
            double d = static_cast<double>(curr);
            last = curr;
            return d;
        };

        total_requests_->Increment(delta_or_reset(metrics.total_requests, last_total_));
        successful_requests_->Increment(delta_or_reset(metrics.successful_requests, last_successful_));
        failed_requests_->Increment(delta_or_reset(metrics.failed_requests, last_failed_));
        bytes_sent_->Increment(delta_or_reset(metrics.total_bytes_sent, last_bytes_sent_));
        bytes_received_->Increment(delta_or_reset(metrics.total_bytes_received, last_bytes_received_));
        
        // Update gauges
        rps_gauge_->Set(collector.requests_per_second());
        error_rate_gauge_->Set(collector.error_rate());
        
        // last_ values updated inside delta_or_reset()
    }
    
    std::string endpoint() const {
        return "http://" + bind_address_ + "/metrics";
    }
    
private:
    std::string bind_address_;
    mutable std::mutex mtx_;
    std::unique_ptr<prometheus::Exposer> exposer_;
    std::shared_ptr<prometheus::Registry> registry_;
    
    prometheus::Counter* total_requests_ = nullptr;
    prometheus::Counter* successful_requests_ = nullptr;
    prometheus::Counter* failed_requests_ = nullptr;
    prometheus::Counter* bytes_sent_ = nullptr;
    prometheus::Counter* bytes_received_ = nullptr;
    prometheus::Histogram* latency_histogram_ = nullptr;
    prometheus::Gauge* rps_gauge_ = nullptr;
    prometheus::Gauge* error_rate_gauge_ = nullptr;
    
    // Last values for delta updates
    uint64_t last_total_ = 0;
    uint64_t last_successful_ = 0;
    uint64_t last_failed_ = 0;
    uint64_t last_bytes_sent_ = 0;
    uint64_t last_bytes_received_ = 0;
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

#endif // CPLOAD_HAVE_PROMETHEUS