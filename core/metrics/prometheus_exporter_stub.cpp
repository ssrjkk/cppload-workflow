#include "cppload/metrics/prometheus_exporter.hpp"

#ifndef CPLOAD_HAVE_PROMETHEUS

namespace cppload::metrics {

class PrometheusExporterImpl {};

PrometheusExporter::PrometheusExporter(const std::string&)
    : impl_(std::make_unique<PrometheusExporterImpl>()) {}

PrometheusExporter::~PrometheusExporter() = default;

bool PrometheusExporter::start() { return false; }
void PrometheusExporter::stop() {}
bool PrometheusExporter::is_running() const { return false; }
void PrometheusExporter::update_metrics(const MetricsCollector&) {}
std::string PrometheusExporter::endpoint() const { return "disabled"; }

} // namespace cppload::metrics

#endif
