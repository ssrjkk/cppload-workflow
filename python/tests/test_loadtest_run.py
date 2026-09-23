# @author ssrjkk | volley
"""Tests for LoadTest run and worker methods."""

import tempfile
import os
from unittest.mock import patch
from volley import LoadTest


class TestLoadTestRun:
    """Test LoadTest run and worker methods."""

    @patch("volley.HttpClient.request")
    def test_loadtest_run(self, mock_request):
        """Test LoadTest run method."""
        mock_request.return_value = {
            "status_code": 200,
            "body": "OK",
            "headers": {},
            "latency_us": 1000,
        }

        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: run_test
target:
  base_url: http://example.com
load_profile:
  - stage: test
    duration: 1s
    target_rps: 10
    concurrent_users: 2
scenarios:
  - name: test_scenario
    weight: 100
    steps:
      - http:
          method: GET
          path: /api/test
""")
            f.flush()
            config_path = f.name

        try:
            test = LoadTest(config_path)
            test.run()

            # Verify metrics were recorded
            assert test.metrics._total_requests > 0
        finally:
            os.unlink(config_path)

    @patch("volley.HttpClient.request")
    def test_loadtest_worker(self, mock_request):
        """Test LoadTest _worker method."""
        mock_request.return_value = {
            "status_code": 200,
            "body": "OK",
            "headers": {},
            "latency_us": 1000,
        }

        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: worker_test
target:
  base_url: http://example.com
load_profile:
  - stage: test
    duration: 1s
    target_rps: 100
scenarios:
  - name: test_scenario
    steps:
      - http:
          method: GET
          path: /api/test
""")
            f.flush()
            config_path = f.name

        try:
            test = LoadTest(config_path)

            scenarios = test.config.get("scenarios", [])
            test._worker(scenarios, "http://example.com")

            assert test.metrics._total_requests > 0
        finally:
            os.unlink(config_path)

    @patch("volley.HttpClient.request")
    def test_loadtest_worker_with_rate_limit(self, mock_request):
        """Test LoadTest _worker with rate limiting."""
        mock_request.return_value = {
            "status_code": 200,
            "body": "OK",
            "headers": {},
            "latency_us": 1000,
        }

        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: rate_limit_test
target:
  base_url: http://example.com
load_profile:
  - stage: test
    duration: 1s
    target_rps: 5
scenarios:
  - name: test_scenario
    steps:
      - http:
          method: GET
          path: /
""")
            f.flush()
            config_path = f.name

        try:
            test = LoadTest(config_path)
            assert test.token_bucket is not None

            scenarios = test.config.get("scenarios", [])
            test._worker(scenarios, "http://example.com")

            assert test.metrics._total_requests > 0
        finally:
            os.unlink(config_path)

    def test_loadtest_validate_sla_invalid_format(self):
        """Test SLA validation with invalid format."""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: sla_invalid_test
target:
  base_url: http://example.com
sla:
  error_rate: "invalid"
  p99_latency: "invalid"
load_profile:
  - stage: test
    duration: 10s
    target_rps: 100
""")
            f.flush()
            config_path = f.name

        try:
            test = LoadTest(config_path)

            # Record some requests
            for _ in range(100):
                test.metrics.record_request(200, 100000)

            # Should use defaults and pass
            result = test.validate_sla()
            assert result is True
        finally:
            os.unlink(config_path)

    def test_loadtest_no_load_profile(self):
        """Test LoadTest with no load profile."""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: no_profile_test
target:
  base_url: http://example.com
""")
            f.flush()
            config_path = f.name

        try:
            test = LoadTest(config_path)

            assert test.token_bucket is None
        finally:
            os.unlink(config_path)


class TestTokenBucketRefill:
    """Test TokenBucket refill edge cases."""

    def test_token_bucket_refill_cap(self):
        """Test token bucket refill caps at burst."""
        from volley import TokenBucket
        import time

        bucket = TokenBucket(rate=10.0, burst=5.0)

        # Consume all tokens
        for _ in range(5):
            bucket.try_consume()

        assert bucket.tokens < 1.0

        # Wait for refill
        time.sleep(0.6)  # Should add 6 tokens, but cap at 5

        # Manually trigger refill
        bucket._refill()

        # Should be capped at burst (5.0)
        assert bucket.tokens <= 5.0
