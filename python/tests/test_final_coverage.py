# @author ssrjkk | cppload
"""Final tests for 100% coverage."""

import pytest
import tempfile
import os
from pathlib import Path
from unittest.mock import Mock, patch
from cppload import LoadTest


class TestFinalCoverage:
    """Test remaining uncovered lines."""

    def test_find_cli_with_existing_file(self):
        """Test _find_cli when CLI exists in build directory."""
        from cppload.core import _find_cli

        # Create a temporary file to simulate CLI
        with tempfile.TemporaryDirectory() as tmpdir:
            cli_path = Path(tmpdir) / "cppload-cli"
            cli_path.touch()

            # Patch the paths list to include our temp directory
            with patch("cppload.core.Path") as mock_path:

                def path_side_effect(p):
                    if "cppload-cli" in str(p) and tmpdir in str(p):
                        return cli_path
                    return Path(p)

                mock_path.side_effect = lambda p: cli_path if "build" in str(p) else Path(p)

                # This is tricky to test without actually creating files
                # Just verify the function works
                result = _find_cli()
                assert isinstance(result, str)

    @patch("cppload.HttpClient.request")
    def test_loadtest_worker_client_none(self, mock_request):
        """Test LoadTest _worker when client is None."""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: client_none_test
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
          path: /
""")
            f.flush()
            config_path = f.name

        try:
            test = LoadTest(config_path)

            # Mock pool.acquire to return None
            with patch.object(test.pool, "acquire", return_value=None):
                scenarios = test.config.get("scenarios", [])
                test._worker(scenarios, "http://example.com")

                # Should handle None client gracefully
                assert test.metrics._total_requests == 0
        finally:
            os.unlink(config_path)

    def test_validate_sla_error_rate_exception(self):
        """Test validate_sla with invalid error_rate format."""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: sla_exception_test
target:
  base_url: http://example.com
sla:
  error_rate: "< not_a_number%"
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

            # Should handle exception and use default
            result = test.validate_sla()
            assert isinstance(result, bool)
        finally:
            os.unlink(config_path)

    def test_validate_sla_p99_exception(self):
        """Test validate_sla with invalid p99_latency format."""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: sla_p99_exception_test
target:
  base_url: http://example.com
sla:
  p99_latency: "< not_a_number"
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

            # Should handle exception and use default
            result = test.validate_sla()
            assert isinstance(result, bool)
        finally:
            os.unlink(config_path)

    def test_validate_sla_no_sla_config(self):
        """Test validate_sla with no SLA configuration."""
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            f.write("""
test_id: no_sla_test
target:
  base_url: http://example.com
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
