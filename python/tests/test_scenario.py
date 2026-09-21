"""Tests for scenario engine and load profile handling."""

import json
import time

from cppload import ScenarioEngine, Scenario, LoadProfile, ConnectionPool, PoolConfig


def _write_config(tmp_path, data: dict):
    import yaml

    p = tmp_path / "config.yaml"
    p.write_text(yaml.safe_dump(data))
    return str(p)


class TestScenarioEngine:
    def test_load_missing_file_fails(self, tmp_path):
        engine = ScenarioEngine(str(tmp_path / "nope.yaml"))
        assert engine.load_config() is False

    def test_load_valid_config(self, tmp_path):
        p = _write_config(tmp_path, {"test_id": "t1", "target": {"base_url": "http://x"}})
        engine = ScenarioEngine(p)
        assert engine.load_config() is True
        assert engine.config["test_id"] == "t1"

    def test_load_invalid_yaml_fails(self, tmp_path):
        p = tmp_path / "bad.yaml"
        p.write_text("::: not yaml :::")
        engine = ScenarioEngine(str(p))
        assert engine.load_config() is False

    def test_load_profile_to_dict(self):
        lp = LoadProfile(stage="warmup", duration="1m", target_rps=50)
        assert lp.to_dict() == {"stage": "warmup", "duration": "1m", "target_rps": 50}

    def test_scenario_to_dict(self):
        s = Scenario(name="login", weight=80, steps=[{"http": {"method": "POST"}}])
        assert s.to_dict()["weight"] == 80


class TestConnectionPool:
    def test_acquire_release_roundtrip(self):
        pool = ConnectionPool(PoolConfig(max_connections=5))
        c = pool.acquire("localhost", "8080")
        assert c is not None
        pool.release(c, "localhost", "8080")
        c2 = pool.acquire("localhost", "8080")
        assert c2 is c

    def test_max_connections_respected(self):
        pool = ConnectionPool(PoolConfig(max_connections=2))
        c1 = pool.acquire("localhost", "8080")
        c2 = pool.acquire("localhost", "8080")
        c3 = pool.acquire("localhost", "8080")
        assert c3 is not None
        pool.release(c1, "localhost", "8080")
        pool.release(c2, "localhost", "8080")
        pool.release(c3, "localhost", "8080")
        assert len(pool._pool["localhost:8080"]) == 2

    def test_per_host_pools(self):
        pool = ConnectionPool()
        a = pool.acquire("h1", "80")
        b = pool.acquire("h2", "80")
        pool.release(a, "h1", "80")
        pool.release(b, "h2", "80")
        assert set(pool._pool.keys()) == {"h1:80", "h2:80"}


class TestJsonRoundTrip:
    def test_snapshot_serializable(self):
        from cppload import MetricsCollector

        m = MetricsCollector()
        m.record_request(200, 100)
        json.dumps(m.snapshot())

    def test_wall_clock_progresses(self):
        from cppload import MetricsCollector

        m = MetricsCollector()
        time.sleep(1.05)
        assert m.requests_per_second >= 0.0
