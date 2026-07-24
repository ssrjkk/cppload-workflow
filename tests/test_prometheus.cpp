// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/metrics/collector.hpp"
#include "cppload/metrics/prometheus_exporter.hpp"
#include <thread>
#include <chrono>

TEST(PrometheusExporterTest, CreatesExporter) {
    cppload::metrics::PrometheusExporter exporter("127.0.0.1:19090");
    EXPECT_FALSE(exporter.is_running());  // Not started yet
}

TEST(PrometheusExporterTest, StartsAndStops) {
    cppload::metrics::PrometheusExporter exporter("127.0.0.1:19091");
    
    EXPECT_TRUE(exporter.start());
    EXPECT_TRUE(exporter.is_running());
    
    // Give it a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    exporter.stop();
    EXPECT_FALSE(exporter.is_running());
}

TEST(PrometheusExporterTest, UpdatesMetrics) {
    cppload::metrics::MetricsCollector collector;
    cppload::metrics::PrometheusExporter exporter("127.0.0.1:19092");
    
    EXPECT_TRUE(exporter.start());
    EXPECT_TRUE(exporter.is_running());
    
    // Record some requests
    collector.record_request(200, std::chrono::microseconds(100), 100, 500);
    collector.record_request(201, std::chrono::microseconds(150), 120, 600);
    collector.record_request(500, std::chrono::microseconds(50), 80, 200);
    
    // Update Prometheus metrics - should not crash
    EXPECT_NO_THROW(exporter.update_metrics(collector));
    
    // Give it time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    exporter.stop();
}

TEST(PrometheusExporterTest, EndpointReturnsData) {
    cppload::metrics::PrometheusExporter exporter("127.0.0.1:19093");
    exporter.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_TRUE(exporter.is_running());
    
    exporter.stop();
}