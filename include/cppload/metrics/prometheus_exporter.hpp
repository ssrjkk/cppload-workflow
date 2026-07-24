#pragma once

#include "collector.hpp"
#include <memory>
#include <string>
#include <cstdint>

namespace cppload::metrics {

// Forward declaration for PIMPL
class PrometheusExporterImpl;

// HTTP server that exposes /metrics endpoint for Prometheus scraping
class PrometheusExporter {
public:
    // Constructor with bind address (default: 0.0.0.0:9090)
    explicit PrometheusExporter(const std::string& bind_address = "0.0.0.0:9090");
    ~PrometheusExporter();
    
    PrometheusExporter(const PrometheusExporter&) = delete;
    PrometheusExporter& operator=(const PrometheusExporter&) = delete;
    PrometheusExporter(PrometheusExporter&&) = delete;
    PrometheusExporter& operator=(PrometheusExporter&&) = delete;
    
    // Start the HTTP server (non-blocking)
    [[nodiscard]] bool start();
    
    // Stop the HTTP server
    void stop();
    
    // Check if server is running
    [[nodiscard]] bool is_running() const;
    
    // Update metrics from collector (call periodically or on each request)
    void update_metrics(const MetricsCollector& collector);
    
    // Get the metrics endpoint URL
    [[nodiscard]] std::string endpoint() const;
    
private:
    std::unique_ptr<PrometheusExporterImpl> impl_;
};

} // namespace cppload::metrics
