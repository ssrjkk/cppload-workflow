#include "cppload/metrics/prometheus_exporter.hpp"
#include <string>

#ifndef CPLOAD_HAVE_PROMETHEUS

namespace cppload::metrics {

class PrometheusExporterImpl {
public:
    std::string bind_address;
};

PrometheusExporter::PrometheusExporter(const std::string& bind_address)
    : impl_(std::make_unique<PrometheusExporterImpl>()) {
    impl_->bind_address = bind_address;
}

PrometheusExporter::~PrometheusExporter() = default;

bool PrometheusExporter::start() { return false; }
void PrometheusExporter::stop() {}
bool PrometheusExporter::is_running() const { return false; }
void PrometheusExporter::update_metrics(const MetricsCollector&) {}
std::string PrometheusExporter::endpoint() const {
    return "http://" + impl_->bind_address + "/metrics (stub — install prometheus-cpp)";
}

} // namespace cppload::metrics

#endif
