# @author ssrjkk | volley
"""Integration tests for CLI with real subprocess execution"""

import pytest
import tempfile
import os
import yaml
import subprocess
from pathlib import Path
from volley import ScenarioEngine


@pytest.fixture
def temp_config():
    """Create temporary YAML config files for testing"""
    configs = {}

    def _create(name, content):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
            yaml.dump(content, f)
            configs[name] = f.name
        return configs[name]

    yield _create

    for path in configs.values():
        try:
            os.unlink(path)
        except Exception:
            pass


@pytest.fixture
def mock_cli_path():
    """Get path to mock CLI script"""
    mock_path = Path(__file__).parent.parent.parent / "tools" / "mock_cli.py"
    if mock_path.exists():
        return str(mock_path)
    return None


def test_scenario_engine_load_valid_config(temp_config):
    """Test loading a valid YAML configuration"""
    config_content = {
        "test_id": "integration-test-001",
        "target": {"base_url": "http://localhost:8080"},
        "load_profile": [{"stage": "ramp", "duration": "10s", "target_rps": 100}],
        "scenarios": [
            {
                "name": "test-scenario",
                "weight": 100,
                "steps": [{"http": {"method": "GET", "path": "/api/test"}}],
            }
        ],
    }

    config_path = temp_config("valid", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert engine.config["test_id"] == "integration-test-001"
    assert engine.config["target"]["base_url"] == "http://localhost:8080"
    assert len(engine.config["load_profile"]) == 1
    assert engine.config["load_profile"][0]["target_rps"] == 100


def test_scenario_engine_load_invalid_config():
    """Test loading a non-existent configuration file"""
    engine = ScenarioEngine("/nonexistent/path/config.yaml")

    result = engine.load_config()

    assert result is False


def test_scenario_engine_load_malformed_yaml(temp_config):
    """Test loading a malformed YAML file"""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".yaml", delete=False) as f:
        f.write("invalid: yaml: content: [unterminated")
        malformed_path = f.name

    try:
        engine = ScenarioEngine(malformed_path)
        result = engine.load_config()
        assert result is False
    finally:
        os.unlink(malformed_path)


def test_scenario_engine_config_property(temp_config):
    """Test config property returns loaded configuration"""
    config_content = {
        "test_id": "property-test",
        "target": {"base_url": "http://example.com"},
    }

    config_path = temp_config("property", config_content)
    engine = ScenarioEngine(config_path)
    engine.load_config()

    config = engine.config

    assert config["test_id"] == "property-test"
    assert config["target"]["base_url"] == "http://example.com"


def test_scenario_engine_complex_config(temp_config):
    """Test loading complex configuration with multiple scenarios"""
    config_content = {
        "test_id": "complex-test",
        "target": {"base_url": "http://api.example.com"},
        "load_profile": [
            {"stage": "warmup", "duration": "5s", "target_rps": 50},
            {"stage": "load", "duration": "30s", "target_rps": 200},
            {"stage": "cooldown", "duration": "5s", "target_rps": 50},
        ],
        "scenarios": [
            {
                "name": "read-heavy",
                "weight": 70,
                "steps": [
                    {"http": {"method": "GET", "path": "/api/users"}},
                    {"http": {"method": "GET", "path": "/api/posts"}},
                ],
            },
            {
                "name": "write-heavy",
                "weight": 30,
                "steps": [
                    {
                        "http": {
                            "method": "POST",
                            "path": "/api/users",
                            "body": '{"name": "test"}',
                        }
                    }
                ],
            },
        ],
    }

    config_path = temp_config("complex", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert len(engine.config["load_profile"]) == 3
    assert len(engine.config["scenarios"]) == 2
    assert engine.config["scenarios"][0]["weight"] == 70
    assert engine.config["scenarios"][1]["weight"] == 30


def test_cli_version_command(mock_cli_path):
    """Test CLI --version command"""
    if mock_cli_path is None:
        pytest.skip("Mock CLI not found")

    result = subprocess.run(
        ["python", mock_cli_path, "--version"],
        capture_output=True,
        text=True,
        timeout=5,
    )

    assert result.returncode == 0
    assert "volley" in result.stdout
    assert "1.1.0" in result.stdout


def test_cli_help_command(mock_cli_path):
    """Test CLI --help command"""
    if mock_cli_path is None:
        pytest.skip("Mock CLI not found")

    result = subprocess.run(
        ["python", mock_cli_path, "--help"],
        capture_output=True,
        text=True,
        timeout=5,
    )

    assert result.returncode == 0
    assert "Usage:" in result.stdout
    assert "Options:" in result.stdout
    assert "--version" in result.stdout
    assert "--help" in result.stdout


def test_cli_invalid_option(mock_cli_path):
    """Test CLI with invalid option"""
    if mock_cli_path is None:
        pytest.skip("Mock CLI not found")

    result = subprocess.run(
        ["python", mock_cli_path, "--invalid-option"],
        capture_output=True,
        text=True,
        timeout=5,
    )

    assert result.returncode != 0
    assert "Unknown option" in result.stderr


def test_cli_no_arguments(mock_cli_path):
    """Test CLI with no arguments"""
    if mock_cli_path is None:
        pytest.skip("Mock CLI not found")

    result = subprocess.run(
        ["python", mock_cli_path],
        capture_output=True,
        text=True,
        timeout=5,
    )

    assert result.returncode != 0
    assert "Usage:" in result.stderr


def test_scenario_engine_with_authentication(temp_config):
    """Test configuration with authentication settings"""
    config_content = {
        "test_id": "auth-test",
        "target": {"base_url": "http://localhost:8080"},
        "authentication": {
            "type": "oauth2",
            "token_endpoint": "http://auth.example.com/token",
            "client_credentials": {
                "client_id": "test-client",
                "client_secret": "test-secret",
            },
        },
        "load_profile": [{"stage": "test", "duration": "10s", "target_rps": 100}],
    }

    config_path = temp_config("auth", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert engine.config["authentication"]["type"] == "oauth2"
    assert engine.config["authentication"]["client_credentials"]["client_id"] == "test-client"


def test_scenario_engine_with_observability(temp_config):
    """Test configuration with observability settings"""
    config_content = {
        "test_id": "observability-test",
        "target": {"base_url": "http://localhost:8080"},
        "observability": {
            "tracing": {
                "otlp_endpoint": "http://jaeger:4317",
                "sample_rate": 0.1,
            },
            "metrics": {"enabled": True, "interval": "5s"},
        },
        "load_profile": [{"stage": "test", "duration": "10s", "target_rps": 100}],
    }

    config_path = temp_config("observability", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert engine.config["observability"]["tracing"]["otlp_endpoint"] == "http://jaeger:4317"
    assert engine.config["observability"]["tracing"]["sample_rate"] == 0.1


def test_scenario_engine_empty_config(temp_config):
    """Test loading empty configuration"""
    config_content = {}

    config_path = temp_config("empty", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert engine.config == {}


def test_scenario_engine_with_environment_variables(temp_config):
    """Test configuration with environment variable references"""
    os.environ["TEST_BASE_URL"] = "http://env-test.example.com"

    config_content = {
        "test_id": "env-test",
        "target": {"base_url": "${TEST_BASE_URL}"},
        "load_profile": [{"stage": "test", "duration": "10s", "target_rps": 100}],
    }

    config_path = temp_config("env", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert engine.config["target"]["base_url"] == "${TEST_BASE_URL}"

    del os.environ["TEST_BASE_URL"]


def test_cli_multiple_commands(mock_cli_path):
    """Test multiple CLI commands in sequence"""
    if mock_cli_path is None:
        pytest.skip("Mock CLI not found")

    commands = [
        ["--version"],
        ["--help"],
        ["--version"],
    ]

    for cmd in commands:
        result = subprocess.run(
            ["python", mock_cli_path] + cmd,
            capture_output=True,
            text=True,
            timeout=5,
        )
        assert result.returncode == 0


def test_scenario_engine_large_config(temp_config):
    """Test loading large configuration with many scenarios"""
    scenarios = []
    for i in range(100):
        scenarios.append(
            {
                "name": f"scenario-{i}",
                "weight": 1,
                "steps": [{"http": {"method": "GET", "path": f"/api/test/{i}"}}],
            }
        )

    config_content = {
        "test_id": "large-test",
        "target": {"base_url": "http://localhost:8080"},
        "load_profile": [{"stage": "test", "duration": "10s", "target_rps": 100}],
        "scenarios": scenarios,
    }

    config_path = temp_config("large", config_content)
    engine = ScenarioEngine(config_path)

    result = engine.load_config()

    assert result is True
    assert len(engine.config["scenarios"]) == 100
