# volley

**High-performance load testing for modern HTTP services**

[![CI](https://github.com/ssrjkk/volley/actions/workflows/ci.yml/badge.svg)](https://github.com/ssrjkk/volley/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/ssrjkk/volley/branch/main/graph/badge.svg)](https://codecov.io/gh/ssrjkk/volley)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support/20)

[Русский](README_RU.md)

---

Enterprise-grade load testing built on C++20 async core. YAML-driven scenarios, OAuth2/Vault/mTLS security, full observability with OpenTelemetry and Prometheus.

## Why volley?

- **Blazing fast** — Lock-free metrics, async I/O with Boost.Beast/ASIO, connection pooling
- **Production-ready** — 8-job CI with ASan/TSan, 100% test coverage, zero warnings
- **Flexible** — HTTP/1.1, WebSocket, raw TCP, custom protocols via factory pattern
- **Observable** — OTLP/HTTP+JSON tracing, Prometheus /metrics, SLA validation
- **Secure** — OAuth2, HashiCorp Vault, mTLS, API keys out of the box

## Quick start

### Build from source

```bash
# Ubuntu 24.04
sudo apt-get install -y cmake ninja-build g++-13 \
  libboost-all-dev libssl-dev libyaml-cpp-dev

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

### Docker

```bash
docker build -t volley:latest -f deploy/docker/Dockerfile .
docker run --rm volley:latest --help
```

### Run your first test

```bash
./build/tools/volley \
  --config=scenarios/ecommerce/load-test.yaml \
  --rps=5000 \
  --duration=300
```

## Example scenario

```yaml
version: "1.0"
test_id: "checkout-stress-2026"

target:
  base_url: ${TARGET_URL:-http://gateway:8080}

load_profile:
  - stage: rampup
    duration: 5m
    target_rps: 1000
  - stage: steady
    duration: 30m
    target_rps: 5000
  - stage: spike
    duration: 2m
    target_rps: 15000

scenarios:
  - name: "user_checkout_flow"
    weight: 70
    steps:
      - http:
          method: GET
          path: "/api/v1/products"
      - http:
          method: POST
          path: "/api/v1/cart"
          body: '{"item_id": "123e4567"}'
          headers:
            Content-Type: application/json
          assertions:
            - status_code == 201

sla:
  error_rate: "< 1%"
  p99_latency: "< 500ms"
```

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  CLI Tool / HTTP Worker / Python SDK                     │
├──────────────────────────────────────────────────────────┤
│  Scenario Engine                                         │
│  • YAML parser with env vars & load profiles            │
│  • TokenBucket rate limiter (thread-safe)               │
│  • SLA validation & step callbacks                      │
└────────────────────┬─────────────────────────────────────┘
                     │
┌────────────────────▼─────────────────────────────────────┐
│  HTTP Client (Boost.Beast + ASIO)                       │
│  • Async request/resolve/connect                        │
│  • Connection pool (acquire/release, idle cleanup)      │
│  • mTLS / TLS Context                                   │
│  • URL encoding + CR/LF sanitization                    │
│  • Raw TCP / WebSocket / Protocol Factory               │
└────────────────────┬─────────────────────────────────────┘
                     │
┌────────────────────▼─────────────────────────────────────┐
│  Metrics & Observability                                │
│  • Lock-free MetricsCollector (p50/p95/p99)             │
│  • OTLP/HTTP+JSON Exporter (thread-safe)                │
│  • Prometheus /metrics endpoint                         │
└──────────────────────────────────────────────────────────┘
```

## Features

| Category | Capabilities |
|----------|--------------|
| **Protocols** | HTTP/1.1, WebSocket (ws/wss), Raw TCP/TLS, custom via `register_protocol()` |
| **Security** | OAuth2 client_credentials, HashiCorp Vault KV v2, mTLS, API keys, Bearer tokens |
| **Observability** | OpenTelemetry OTLP/HTTP+JSON, Prometheus counters/histograms, SLA validation |
| **Performance** | Lock-free metrics, connection pooling, TokenBucket rate limiting, async I/O |
| **Deployment** | Docker multi-stage, Kubernetes Helm charts, Prometheus + Grafana dashboards |

## Tech stack

**Core:** C++20, Boost.Beast/ASIO, OpenSSL, yaml-cpp, nlohmann_json  
**Testing:** GoogleTest (189 tests, 21 suites), 100% Python SDK coverage  
**CI/CD:** GitHub Actions (8 jobs), ASan/TSan, clang-tidy, codecov  
**Infra:** Docker, Kubernetes/Helm, Prometheus, Grafana, Jaeger  
**SDK:** Python (pure Python, urllib-based, 143 tests)

## CI Pipeline

Every commit runs through 8 CI jobs:

- **Build & Test** — Release build + 189 GTest tests
- **Coverage** — Debug + --coverage + codecov upload
- **AddressSanitizer** — ASan + UBSan memory safety
- **ThreadSanitizer** — TSan data race detection
- **Performance Regression** — Benchmarks vs baseline
- **Integration** — Mock server, connection pool, OAuth2, Vault
- **E2E Smoke** — Demo services + CLI, SLA validation
- **Lint** — clang-tidy, black, flake8, mypy, YAML validation

## Documentation

- [Contributing](CONTRIBUTING.md) — Development guidelines
- [Architecture](docs/) — ADR and design docs
- [Scenarios](scenarios/) — Example YAML configurations
- [Grafana Dashboards](deploy/grafana/) — Pre-built monitoring

## License

Apache 2.0 — see [LICENSE](LICENSE)

---

## Author

**ssrjkk**  
Telegram: [@ssrjkk](https://t.me/ssrjkk)  
Email: ray013lefe@gmail.com
