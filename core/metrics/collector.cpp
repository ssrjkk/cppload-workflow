#include "cppload/metrics/collector.hpp"
#include <algorithm>
#include <vector>
#include <cmath>

namespace cppload::metrics {

void MetricsCollector::record_request(uint16_t status_code,
                                      std::chrono::microseconds latency,
                                      size_t bytes_sent,
                                      size_t bytes_received) {
    total_requests_.fetch_add(1, std::memory_order_relaxed);
    total_bytes_sent_.fetch_add(bytes_sent, std::memory_order_relaxed);
    total_bytes_received_.fetch_add(bytes_received, std::memory_order_relaxed);
    
    cumulative_latency_us_.fetch_add(latency.count(), std::memory_order_relaxed);
    
    if (status_code >= 200 && status_code < 400) {
        successful_requests_.fetch_add(1, std::memory_order_relaxed);
    } else {
        failed_requests_.fetch_add(1, std::memory_order_relaxed);
    }
    
    auto lat_val = latency.count();
    auto min_curr = min_latency_us_.load(std::memory_order_relaxed);
    while (lat_val < min_curr && 
           !min_latency_us_.compare_exchange_weak(min_curr, lat_val)) {}
    
    auto max_curr = max_latency_us_.load(std::memory_order_relaxed);
    while (lat_val > max_curr && 
           !max_latency_us_.compare_exchange_weak(max_curr, lat_val)) {}
}

RequestMetrics MetricsCollector::snapshot() const {
    RequestMetrics m;
    m.total_requests = total_requests_.load(std::memory_order_relaxed);
    m.successful_requests = successful_requests_.load(std::memory_order_relaxed);
    m.failed_requests = failed_requests_.load(std::memory_order_relaxed);
    m.total_bytes_sent = total_bytes_sent_.load(std::memory_order_relaxed);
    m.total_bytes_received = total_bytes_received_.load(std::memory_order_relaxed);
    
    auto cum_lat = cumulative_latency_us_.load(std::memory_order_relaxed);
    if (m.total_requests > 0) {
        m.mean_latency_us = static_cast<double>(cum_lat) / m.total_requests;
    }
    
    m.min_latency = std::chrono::microseconds(
        min_latency_us_.load(std::memory_order_relaxed));
    m.max_latency = std::chrono::microseconds(
        max_latency_us_.load(std::memory_order_relaxed));
    
    return m;
}

double MetricsCollector::requests_per_second() const {
    auto now = std::chrono::steady_clock::now();
    auto start = std::chrono::steady_clock::time_point(
        std::chrono::steady_clock::duration(
            start_time_.load(std::memory_order_relaxed)));
    auto elapsed = std::chrono::duration<double>(now - start).count();
    
    if (elapsed < 0.001) return 0.0;
    return total_requests_.load(std::memory_order_relaxed) / elapsed;
}

double MetricsCollector::error_rate() const {
    auto total = total_requests_.load(std::memory_order_relaxed);
    if (total == 0) return 0.0;
    return static_cast<double>(failed_requests_.load(std::memory_order_relaxed)) / total * 100.0;
}

uint64_t MetricsCollector::p95_latency_us() const {
    return static_cast<uint64_t>(snapshot().mean_latency_us * 1.5);
}

uint64_t MetricsCollector::p99_latency_us() const {
    return static_cast<uint64_t>(snapshot().mean_latency_us * 2.0);
}

void MetricsCollector::reset() {
    total_requests_.store(0, std::memory_order_relaxed);
    successful_requests_.store(0, std::memory_order_relaxed);
    failed_requests_.store(0, std::memory_order_relaxed);
    total_bytes_sent_.store(0, std::memory_order_relaxed);
    total_bytes_received_.store(0, std::memory_order_relaxed);
    cumulative_latency_us_.store(0, std::memory_order_relaxed);
    min_latency_us_.store(std::chrono::microseconds::max().count(), std::memory_order_relaxed);
    max_latency_us_.store(std::chrono::microseconds::min().count(), std::memory_order_relaxed);
    start_time_.store(std::chrono::steady_clock::now().time_since_epoch().count(), 
                      std::memory_order_relaxed);
}

} // namespace cppload::metrics
