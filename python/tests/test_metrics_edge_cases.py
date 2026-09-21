# @author ssrjkk | cppload
"""Additional edge case tests for MetricsCollector."""

import pytest
from cppload import MetricsCollector


class TestMetricsCollectorEdgeCases:
    """Test MetricsCollector edge cases for 100% coverage."""

    def test_requests_per_second_immediate(self):
        """Test RPS calculation immediately after creation."""
        collector = MetricsCollector()
        # Immediately check RPS (elapsed < 0.001)
        rps = collector.requests_per_second
        assert rps >= 0.0

    def test_error_rate_empty(self):
        """Test error rate with no requests."""
        collector = MetricsCollector()
        assert collector.error_rate == 0.0

    def test_error_rate_calculation(self):
        """Test error rate calculation."""
        collector = MetricsCollector()

        # Record 75 successful, 25 failed
        for _ in range(75):
            collector.record_request(200, 1000)
        for _ in range(25):
            collector.record_request(500, 1000)

        error_rate = collector.error_rate
        assert error_rate == 25.0  # 25%

    def test_p95_empty_latencies(self):
        """Test P95 with no latencies."""
        collector = MetricsCollector()
        assert collector.p95_latency_us == 0

    def test_p99_empty_latencies(self):
        """Test P99 with no latencies."""
        collector = MetricsCollector()
        assert collector.p99_latency_us == 0

    def test_p95_single_value(self):
        """Test P95 with single value."""
        collector = MetricsCollector()
        collector.record_request(200, 1000)

        p95 = collector.p95_latency_us
        assert p95 == 1000

    def test_p99_single_value(self):
        """Test P99 with single value."""
        collector = MetricsCollector()
        collector.record_request(200, 1000)

        p99 = collector.p99_latency_us
        assert p99 == 1000

    def test_p95_multiple_values(self):
        """Test P95 with multiple values."""
        collector = MetricsCollector()

        # Record 100 values: 1-100
        for i in range(1, 101):
            collector.record_request(200, i * 1000)

        p95 = collector.p95_latency_us
        # 95th percentile should be around 95000
        assert p95 >= 94000 and p95 <= 96000

    def test_p99_multiple_values(self):
        """Test P99 with multiple values."""
        collector = MetricsCollector()

        # Record 100 values: 1-100
        for i in range(1, 101):
            collector.record_request(200, i * 1000)

        p99 = collector.p99_latency_us
        # 99th percentile should be around 99000
        assert p99 >= 98000 and p99 <= 100000

    def test_snapshot_empty(self):
        """Test snapshot with no data."""
        collector = MetricsCollector()
        snapshot = collector.snapshot()

        assert snapshot["total_requests"] == 0
        assert snapshot["successful_requests"] == 0
        assert snapshot["failed_requests"] == 0
        assert snapshot["mean_latency_us"] == 0
        assert snapshot["min_latency_us"] == 0
        assert snapshot["max_latency_us"] == 0
        assert snapshot["p95_latency_us"] == 0
        assert snapshot["p99_latency_us"] == 0
        assert snapshot["error_rate"] == 0.0

    def test_snapshot_with_data(self):
        """Test snapshot with data."""
        collector = MetricsCollector()

        collector.record_request(200, 1000, 100, 200)
        collector.record_request(201, 2000, 150, 300)
        collector.record_request(500, 3000, 50, 100)

        snapshot = collector.snapshot()

        assert snapshot["total_requests"] == 3
        assert snapshot["successful_requests"] == 2
        assert snapshot["failed_requests"] == 1
        assert snapshot["total_bytes_sent"] == 300
        assert snapshot["total_bytes_received"] == 600
        assert snapshot["mean_latency_us"] == 2000.0
        assert snapshot["min_latency_us"] == 1000
        assert snapshot["max_latency_us"] == 3000
        assert snapshot["p95_latency_us"] > 0
        assert snapshot["p99_latency_us"] > 0
        assert snapshot["error_rate"] > 0

    def test_latency_cap_eviction(self):
        """Test latency list cap and eviction."""
        collector = MetricsCollector()

        # Record more than 100000 requests
        for i in range(100001):
            collector.record_request(200, i)

        # Should have evicted old entries, keeping last 50000
        assert len(collector._latencies) == 50000
        # First entry should be 50001 (after eviction)
        assert collector._latencies[0] == 50001

    def test_status_code_boundaries(self):
        """Test status code boundary conditions."""
        collector = MetricsCollector()

        # 199 is not successful
        collector.record_request(199, 1000)
        assert collector._failed_requests == 1

        # 200 is successful
        collector.record_request(200, 1000)
        assert collector._successful_requests == 1

        # 399 is successful
        collector.record_request(399, 1000)
        assert collector._successful_requests == 2

        # 400 is not successful
        collector.record_request(400, 1000)
        assert collector._failed_requests == 2

    def test_requests_per_second_after_requests(self):
        """Test RPS after recording requests."""
        import time
        collector = MetricsCollector()

        # Wait a tiny bit to ensure elapsed time > 0.001
        time.sleep(0.002)

        # Record some requests
        for _ in range(100):
            collector.record_request(200, 1000)

        rps = collector.requests_per_second
        assert rps > 0

    def test_reset_clears_all(self):
        """Test reset clears all data."""
        collector = MetricsCollector()

        collector.record_request(200, 1000, 100, 200)
        collector.record_request(500, 2000, 50, 100)

        collector.reset()

        assert collector._total_requests == 0
        assert collector._successful_requests == 0
        assert collector._failed_requests == 0
        assert collector._total_bytes_sent == 0
        assert collector._total_bytes_received == 0
        assert len(collector._latencies) == 0
