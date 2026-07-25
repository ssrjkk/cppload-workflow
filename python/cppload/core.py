# @author ssrjkk | cppload
"""Core Python SDK for cppload-pro - Enterprise Load Testing Platform"""

import os
import yaml
import subprocess
import json
import time
import threading
from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Callable
from pathlib import Path
from enum import Enum


class AuthType(Enum):
    """Supported authentication types."""
    NONE = "none"
    API_KEY = "api_key"
    BEARER_TOKEN = "bearer_token"
    OAUTH2 = "oauth2"
    MTLS = "mtls"


@dataclass
class AuthConfig:
    """Configuration for authentication providers."""
    type: AuthType = AuthType.NONE
    api_key: str = ""
    token: str = ""
    client_id: str = ""
    client_secret: str = ""
    token_endpoint: str = ""
    cert_path: str = ""
    key_path: str = ""
    ca_path: str = ""


@dataclass
class VaultConfig:
    """Configuration for HashiCorp Vault client."""
    address: str = "http://127.0.0.1:8200"
    token: str = ""
    engine_path: str = "secret"
    timeout_seconds: int = 5


@dataclass
class TraceConfig:
    """Configuration for OpenTelemetry tracing."""
    endpoint: str = "http://localhost:4317"
    sample_rate: float = 1.0
    service_name: str = "cppload-pro"
    service_version: str = "1.0.0"


@dataclass
class LoadProfile:
    """A single stage in a load test profile."""
    stage: str
    duration: str
    target_rps: int

    def to_dict(self) -> dict:
        return {"stage": self.stage, "duration": self.duration, "target_rps": self.target_rps}


@dataclass
class PoolConfig:
    """Configuration for connection pool."""
    min_connections: int = 5
    max_connections: int = 100
    idle_timeout: int = 30
    keep_alive: bool = True


@dataclass
class HttpRequest:
    """An HTTP request to be executed by the client."""
    method: str = "GET"
    target: str = "/"
    body: str = ""
    headers: Dict[str, str] = field(default_factory=dict)
    host: str = "localhost"
    port: str = "80"


@dataclass
class Scenario:
    """A named load test scenario with HTTP steps."""
    name: str
    weight: int = 100
    steps: List[Dict[str, Any]] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {"name": self.name, "weight": self.weight, "steps": self.steps}


class MetricsCollector:
    """Thread-safe HTTP request metrics collector with percentile computation."""

    def __init__(self):
        self._total_requests = 0
        self._successful_requests = 0
        self._failed_requests = 0
        self._total_bytes_sent = 0
        self._total_bytes_received = 0
        self._latencies: List[int] = []
        self._lock = threading.Lock()
        self._start_time = time.time()

    def record_request(
        self, status_code: int, latency_us: int, bytes_sent: int = 0, bytes_received: int = 0
    ) -> None:
        with self._lock:
            self._total_requests += 1
            self._total_bytes_sent += bytes_sent
            self._total_bytes_received += bytes_received
            if 200 <= status_code < 400:
                self._successful_requests += 1
            else:
                self._failed_requests += 1
            self._latencies.append(latency_us)
            if len(self._latencies) > 100000:
                self._latencies = self._latencies[-50000:]

    @property
    def requests_per_second(self) -> float:
        with self._lock:
            elapsed = time.time() - self._start_time
            if elapsed < 0.001:
                return 0.0
            return self._total_requests / elapsed

    @property
    def error_rate(self) -> float:
        with self._lock:
            if self._total_requests == 0:
                return 0.0
            return self._failed_requests / self._total_requests * 100.0

    @property
    def p95_latency_us(self) -> int:
        with self._lock:
            if not self._latencies:
                return 0
            sorted_lats = sorted(self._latencies)
            idx = int(len(sorted_lats) * 0.95)
            return sorted_lats[min(idx, len(sorted_lats) - 1)]

    @property
    def p99_latency_us(self) -> int:
        with self._lock:
            if not self._latencies:
                return 0
            sorted_lats = sorted(self._latencies)
            idx = int(len(sorted_lats) * 0.99)
            return sorted_lats[min(idx, len(sorted_lats) - 1)]

    def snapshot(self) -> dict:
        with self._lock:
            return {
                "total_requests": self._total_requests,
                "successful_requests": self._successful_requests,
                "failed_requests": self._failed_requests,
                "total_bytes_sent": self._total_bytes_sent,
                "total_bytes_received": self._total_bytes_received,
                "mean_latency_us": (
                    sum(self._latencies) / len(self._latencies) if self._latencies else 0
                ),
                "min_latency_us": min(self._latencies) if self._latencies else 0,
                "max_latency_us": max(self._latencies) if self._latencies else 0,
                "p95_latency_us": self.p95_latency_us,
                "p99_latency_us": self.p99_latency_us,
                "requests_per_second": self.requests_per_second,
                "error_rate": self.error_rate,
            }

    def reset(self) -> None:
        with self._lock:
            self._total_requests = 0
            self._successful_requests = 0
            self._failed_requests = 0
            self._total_bytes_sent = 0
            self._total_bytes_received = 0
            self._latencies.clear()
            self._start_time = time.time()


class TokenBucket:
    """Thread-safe token bucket rate limiter."""

    def __init__(self, rate: float, burst: float = 0):
        self.rate = rate
        self.burst = burst if burst > 0 else rate
        self.tokens = self.burst
        self.last_refill = time.monotonic()
        self._lock = threading.Lock()
        if rate <= 0:
            raise ValueError("TokenBucket: rate must be > 0")

    def _refill(self):
        now = time.monotonic()
        elapsed = now - self.last_refill
        if elapsed > 0:
            self.tokens += elapsed * self.rate
            if self.tokens > self.burst:
                self.tokens = self.burst
            self.last_refill = now

    def consume(self) -> None:
        while True:
            with self._lock:
                self._refill()
                if self.tokens >= 1.0:
                    self.tokens -= 1.0
                    return
                deficit = 1.0 - self.tokens
                wait_sec = min(deficit / self.rate, 1.0)
            if wait_sec > 0.0:
                time.sleep(wait_sec)

    def try_consume(self) -> bool:
        with self._lock:
            self._refill()
            if self.tokens < 1.0:
                return False
            self.tokens -= 1.0
            return True


class HttpClient:
    """Simple synchronous HTTP client using urllib."""

    def __init__(self):
        self.timeout_ms = 5000
        self.keep_alive = True

    def request(self, req: HttpRequest) -> dict:
        import urllib.request
        import urllib.error

        scheme = "https" if str(req.port) == "443" else "http"
        url = f"{scheme}://{req.host}:{req.port}{req.target}"
        data = req.body.encode() if req.body else None
        headers = req.headers.copy()

        r = urllib.request.Request(url, data=data, headers=headers, method=req.method)
        start = time.time()
        try:
            response = urllib.request.urlopen(r, timeout=self.timeout_ms / 1000)
            body = response.read()
            latency = int((time.time() - start) * 1_000_000)
            return {
                "status_code": response.status,
                "body": body.decode(),
                "headers": dict(response.headers),
                "latency_us": latency,
            }
        except urllib.error.HTTPError as e:
            latency = int((time.time() - start) * 1_000_000)
            return {
                "status_code": e.code,
                "body": e.read().decode(),
                "headers": {},
                "latency_us": latency,
            }
        except Exception as e:
            return {
                "status_code": 0,
                "body": str(e),
                "headers": {},
                "latency_us": 0,
            }


class ConnectionPool:
    """Thread-safe HTTP connection pool."""

    def __init__(self, config: Optional[PoolConfig] = None):
        self.config = config or PoolConfig()
        self._pool: Dict[str, List[HttpClient]] = {}
        self._lock = threading.Lock()

    def acquire(self, host: str, port: str = "80") -> Optional[HttpClient]:
        key = f"{host}:{port}"
        with self._lock:
            clients = self._pool.get(key, [])
            if clients:
                return clients.pop()
        return HttpClient()

    def release(self, client: HttpClient, host: str, port: str = "80"):
        key = f"{host}:{port}"
        with self._lock:
            if key not in self._pool:
                self._pool[key] = []
            if len(self._pool[key]) < self.config.max_connections:
                self._pool[key].append(client)


class AuthProvider:
    """Authentication provider supporting API Key, Bearer, OAuth2."""

    def __init__(self, config: Optional[AuthConfig] = None):
        self.config = config or AuthConfig()
        self._current_token = ""
        self._token_expiry = 0.0
        if self.config.type == AuthType.OAUTH2 and self.config.token_endpoint:
            self._fetch_token()

    def apply_headers(self, headers: Dict[str, str]) -> None:
        if self.config.type == AuthType.API_KEY:
            headers["X-API-Key"] = self.config.api_key
        elif self.config.type == AuthType.BEARER_TOKEN:
            headers["Authorization"] = f"Bearer {self.config.token}"
        elif self.config.type == AuthType.OAUTH2:
            if self._is_expired():
                self._fetch_token()
            headers["Authorization"] = f"Bearer {self._current_token}"

    def _is_expired(self) -> bool:
        return time.time() >= self._token_expiry

    def _fetch_token(self) -> None:
        import urllib.request
        import urllib.parse

        data = urllib.parse.urlencode(
            {
                "grant_type": "client_credentials",
                "client_id": self.config.client_id,
                "client_secret": self.config.client_secret,
            }
        ).encode()

        req = urllib.request.Request(
            self.config.token_endpoint,
            data=data,
            headers={"Content-Type": "application/x-www-form-urlencoded"},
        )

        with urllib.request.urlopen(req, timeout=10) as resp:
            body = json.loads(resp.read())
            self._current_token = body.get("access_token", "")
            expires_in = body.get("expires_in", 3600)
            self._token_expiry = time.time() + max(expires_in - 60, 1)

    def refresh_token(self) -> bool:
        if self.config.type != AuthType.OAUTH2:
            return True
        try:
            self._fetch_token()
            return True
        except Exception:
            return False


class VaultClient:
    """HashiCorp Vault HTTP client for KV v2 secrets."""

    def __init__(self, config: Optional[VaultConfig] = None):
        self.config = config or VaultConfig()
        self._connected = False
        self._last_error = ""
        try:
            self._health_check()
            self._connected = True
        except Exception as e:
            self._connected = False
            self._last_error = str(e)

    @property
    def is_connected(self) -> bool:
        return self._connected

    def get_secret(self, path: str, key: str) -> Optional[str]:
        import urllib.request

        api_path = f"/v1/{self.config.engine_path}/data/{path}"
        url = f"{self.config.address}{api_path}"

        req = urllib.request.Request(url)
        req.add_header("X-Vault-Token", self.config.token)

        try:
            with urllib.request.urlopen(req, timeout=self.config.timeout_seconds) as resp:
                body = json.loads(resp.read())
                data = body.get("data", {}).get("data", {})
                return data.get(key)
        except Exception:
            return None

    def get_secret_map(self, path: str) -> Dict[str, str]:
        import urllib.request

        api_path = f"/v1/{self.config.engine_path}/data/{path}"
        url = f"{self.config.address}{api_path}"

        req = urllib.request.Request(url)
        req.add_header("X-Vault-Token", self.config.token)

        try:
            with urllib.request.urlopen(req, timeout=self.config.timeout_seconds) as resp:
                body = json.loads(resp.read())
                return body.get("data", {}).get("data", {})
        except Exception:
            return {}

    def _health_check(self):
        import urllib.request

        req = urllib.request.Request(f"{self.config.address}/v1/sys/health")
        if self.config.token:
            req.add_header("X-Vault-Token", self.config.token)
        with urllib.request.urlopen(req, timeout=self.config.timeout_seconds) as resp:
            if resp.status >= 500:
                raise ConnectionError("Vault unhealthy")


class Tracer:
    """OpenTelemetry-compatible distributed tracer."""

    def __init__(self, config: Optional[TraceConfig] = None):
        self.config = config or TraceConfig()
        self._trace_id = None
        self._spans: List[dict] = []
        self._active_span = None

    def start_span(self, name: str):
        import uuid

        if self._active_span:
            self.end_span()

        self._active_span = {
            "name": name,
            "trace_id": self._trace_id or uuid.uuid4().hex[:32],
            "span_id": uuid.uuid4().hex[:16],
            "start_time": time.time_ns(),
            "attributes": {},
        }
        if self._trace_id is None:
            self._trace_id = self._active_span["trace_id"]

    def end_span(self):
        if not self._active_span:
            return
        self._active_span["end_time"] = time.time_ns()
        self._spans.append(self._active_span)
        self._active_span = None

    def add_attribute(self, key: str, value: str):
        if self._active_span:
            self._active_span["attributes"][key] = value

    @property
    def trace_id(self) -> str:
        return self._trace_id or ""


def _find_cli() -> str:
    paths = [
        Path("./build/tools/cppload-cli"),
        Path("./build-release/tools/cppload-cli"),
        Path("/usr/local/bin/cppload-cli"),
        Path("/usr/bin/cppload-cli"),
    ]
    for p in paths:
        if p.exists():
            return str(p)
    return "cppload-cli"


class ScenarioEngine:
    """YAML-based scenario loader and executor."""

    def __init__(self, config_path: str):
        self._config_path = config_path
        self._config: Dict[str, Any] = {}

    def load_config(self) -> bool:
        try:
            with open(self._config_path) as f:
                self._config = yaml.safe_load(f)
            return True
        except Exception as e:
            print(f"Config error: {e}")
            return False

    @property
    def config(self) -> dict:
        return self._config

    def run(self, callback: Optional[Callable] = None):
        print(f"Running test: {self._config.get('test_id', 'unknown')}")
        cli = _find_cli()
        cmd = [cli, "--config", self._config_path]
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        if callback:
            callback(result)


class LoadTest:
    """High-level load test orchestrator with metrics, auth, and tracing."""

    def __init__(self, config_path: str):
        with open(config_path) as f:
            self.config = yaml.safe_load(f)

        self.test_id = self.config.get("test_id", "unknown")
        target = self.config.get("target", {})
        self.target_url = os.path.expandvars(target.get("base_url", ""))

        self.metrics = MetricsCollector()
        self.auth = AuthProvider()
        self.vault: Optional[VaultClient] = None
        self.tracer = Tracer()
        self.token_bucket: Optional[TokenBucket] = None
        self.pool = ConnectionPool()

        self._setup_integrations()

    def _setup_integrations(self):
        auth_cfg = self.config.get("authentication", {})
        if auth_cfg.get("type") == "oauth2":
            self.auth = AuthProvider(
                AuthConfig(
                    type=AuthType.OAUTH2,
                    client_id=auth_cfg.get("client_credentials", {}).get("client_id", ""),
                    client_secret=auth_cfg.get("client_credentials", {}).get("client_secret", ""),
                    token_endpoint=auth_cfg.get("token_endpoint", ""),
                )
            )

        tracing = self.config.get("observability", {}).get("tracing", {})
        if tracing.get("otlp_endpoint"):
            self.tracer = Tracer(
                TraceConfig(
                    endpoint=tracing["otlp_endpoint"],
                    sample_rate=tracing.get("sample_rate", 0.1),
                )
            )

        profiles = self.config.get("load_profile", [])
        if profiles:
            target_rps = profiles[0].get("target_rps", 100)
            self.token_bucket = TokenBucket(target_rps)

    def _worker(self, scenarios: list, base_url: str):
        from urllib.parse import urlparse

        parsed = urlparse(base_url)
        host = parsed.hostname or "localhost"
        port = str(parsed.port or 80)

        for scenario in scenarios:
            for step in scenario.get("steps", []):
                http_step = step.get("http", {})
                req = HttpRequest(
                    method=http_step.get("method", "GET"),
                    target=http_step.get("path", "/"),
                    host=host,
                    port=port,
                )

                if self.token_bucket:
                    self.token_bucket.consume()

                client = self.pool.acquire(host, port)
                resp = client.request(req)
                self.pool.release(client, host, port)
                self.metrics.record_request(
                    resp["status_code"],
                    resp.get("latency_us", 0),
                )

    def run(self):
        print(f"Running test: {self.test_id}")
        print(f"Target: {self.target_url}")

        self.tracer.start_span("load_test")
        self.tracer.add_attribute("test_id", self.test_id)

        profiles = self.config.get("load_profile", [])
        concurrency = profiles[0].get("concurrent_users", 10) if profiles else 10

        scenarios = self.config.get("scenarios", [])
        threads = []
        for _ in range(concurrency):
            t = threading.Thread(target=self._worker, args=(scenarios, self.target_url))
            t.start()
            threads.append(t)

        for t in threads:
            t.join()

        self.tracer.end_span()
        self._print_results()

    def validate_sla(self) -> bool:
        sla = self.config.get("sla", {})
        max_error_rate = 0.1
        max_p99_ms = 500

        error_str = sla.get("error_rate", "< 0.1%")
        if "<" in error_str:
            try:
                max_error_rate = float(error_str.replace("<", "").replace("%", "").strip())
            except ValueError:
                pass

        p99_str = sla.get("p99_latency", "< 500ms")
        if "<" in p99_str:
            try:
                value = p99_str.replace("<", "").replace("ms", "").strip()
                max_p99_ms = float(value)
            except ValueError:
                pass

        current_error = self.metrics.error_rate
        current_p99 = self.metrics.p99_latency_us / 1000

        sla_pass = True
        if current_error > max_error_rate:
            print(f"  SLA FAIL: error_rate={current_error:.2f}% > {max_error_rate}%")
            sla_pass = False
        if current_p99 > max_p99_ms:
            print(f"  SLA FAIL: p99={current_p99:.0f}ms > {max_p99_ms}ms")
            sla_pass = False

        return sla_pass

    def _print_results(self):
        m = self.metrics.snapshot()
        print("\nResults:")
        print(f"  Total requests: {m['total_requests']}")
        print(f"  Successful:    {m['successful_requests']}")
        print(f"  Failed:        {m['failed_requests']}")
        print(f"  Error rate:    {m['error_rate']:.2f}%")
        print(f"  Mean latency:  {m['mean_latency_us']:.0f} us")
        print(f"  P95 latency:   {m['p95_latency_us']} us")
        print(f"  P99 latency:   {m['p99_latency_us']} us")
        print(f"  Actual RPS:    {m['requests_per_second']:.0f}")