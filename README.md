# volley

[![CI](https://github.com/ssrjkk/cppload-workflow/actions/workflows/ci.yml/badge.svg)](https://github.com/ssrjkk/cppload-workflow/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/ssrjkk/cppload-workflow/branch/main/graph/badge.svg)](https://codecov.io/gh/ssrjkk/cppload-workflow)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

**[English](#english)** | **[Русский](#русский)**

---

<a id="english"></a>
## English

High-performance HTTP/TCP/WebSocket load testing. C++20 core (Boost.Beast/ASIO), YAML scenarios,
Vault, OAuth2, OpenTelemetry and Prometheus integration.

### Features

| Feature | Status | Details |
|---------|--------|---------|
| **Async HTTP/1.1 Client** | ready | Boost.Beast + ASIO, per-request safety, URL encoding |
| **Raw TCP Client** | ready | Arbitrary bytes over TCP/TLS |
| **WebSocket Client** | ready | ws:// + wss://, arbitrary messages |
| **Protocol Factory** | ready | Custom protocols via `register_protocol()` |
| **TokenBucket Rate Limiter** | ready | RPS control, consume/try_consume, overflow-safe |
| **Connection Pool** | ready | acquire/release, idle cleanup, stats |
| **YAML Scenario Engine** | ready | yaml-cpp, env vars `${VAR:-default}`, SLA validation |
| **OAuth2 Client Credentials** | ready | HTTP POST, JSON, auto-refresh, URL encoding |
| **HashiCorp Vault** | ready | KV v2, AppRole, database creds, health check, path sanitization |
| **mTLS** | ready | Mutual TLS authentication |
| **TLS Context** | ready | Central TLS config for outbound connections |
| **OpenTelemetry OTLP** | ready | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus Exporter** | ready¹ | /metrics endpoint, counters, histograms, gauges |
| **CLI Tool** | ready | Run load tests from command line |
| **HTTP Worker** | ready | Standalone worker, CLI args only |
| **Helm Charts** | ready | K8s deployment |
| **Docker Multi-stage** | ready | Multi-stage runtime image, Ubuntu 24.04, non-root user |
| **Python SDK** | ready | urllib-based (pure Python), 100% coverage, 143 tests |
| **gRPC Worker** | ready² | Load management via gRPC control plane |

> ¹ Full counters/histograms require `prometheus-cpp` at build time. Without it — embedded Boost.Beast HTTP server (text /metrics endpoint).
> ² Requires `gRPC` and `Protobuf` at build time (auto-detected).

### Quick start

#### Bash (system packages, no Conan)

```bash
# Ubuntu 24.04
sudo apt-get install -y cmake ninja-build g++-13 \
  libboost-all-dev libssl-dev libyaml-cpp-dev

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVOLLEY_BUILD_TESTS=ON \
  -DVOLLEY_BUILD_TOOLS=ON

cmake --build build
cd build && ctest --output-on-failure
```

#### Docker (Conan-based)

```bash
docker build -t volley:latest -f deploy/docker/Dockerfile .
docker run --rm volley:latest --help
```

#### Running a load test

```bash
# From YAML scenario
./build/tools/volley \
  --config=scenarios/ecommerce/load-test.yaml \
  --rps=5000 \
  --duration=300

# With OAuth2
./build/tools/volley \
  --config=test.yaml \
  --auth-type=oauth2 \
  --client-id=$CLIENT_ID \
  --client-secret=$CLIENT_SECRET \
  --token-endpoint=https://auth.company.com/oauth/token

# With Vault
./build/tools/volley \
  --config=test.yaml \
  --vault-addr=https://vault.company.com:8200 \
  --vault-token=$VAULT_TOKEN

# With OTLP tracing
./build/tools/volley \
  --config=test.yaml \
  --otlp-endpoint=http://jaeger:4318
```

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│  CLI Tool / HTTP Worker / Python SDK                     │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Scenario Engine                                 │    │
│  │  • YAML parser (env vars, load profiles, SLA)    │    │
│  │  • TokenBucket rate limiter                      │    │
│  │  • Step callback pipeline                        │    │
│  └────────────────────┬─────────────────────────────┘    │
│                       │                                  │
│  ┌────────────────────▼─────────────────────────────┐    │
│  │  HTTP Client (Boost.Beast + ASIO)                │    │
│  │  • Async request/resolve/connect                 │    │
│  │  • Connection pool (acquire/release)             │    │
│  │  • mTLS / TLS Context                            │    │
│  │  • URL encoding + CR/LF sanitization             │    │
│  │  • Raw TCP / WebSocket / Protocol Factory        │    │
│  └────────────────────┬─────────────────────────────┘    │
│                       │                                  │
│  ┌────────────────────▼─────────────────────────────┐    │
│  │  Metrics & Observability                         │    │
│  │  • Lock-free MetricsCollector (p50/p95/p99)      │    │
│  │  • OTLP/HTTP+JSON Exporter (thread-safe)         │    │
│  │  • Prometheus /metrics export                    │    │
│  └──────────────────────────────────────────────────┘    │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Auth & Secrets                                  │    │
│  │  • OAuth2 (client_credentials, auto-refresh)     │    │
│  │  • API Key / Bearer Token                        │    │
│  │  • mTLS                                          │    │
│  │  • HashiCorp Vault (KV v2, AppRole, DB creds)    │    │
│  └──────────────────────────────────────────────────┘    │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
                 [Target HTTP Service]
```

### Example YAML scenario

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

### Integrations

| System | Type | Status |
|--------|------|--------|
| **HashiCorp Vault** | secrets | KV v2 read/write, AppRole login, database creds, health check |
| **OAuth2** | auth | client_credentials grant, auto-refresh, URL encoding |
| **mTLS** | auth | Mutual TLS authentication |
| **OpenTelemetry** | tracing | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus** | metrics | /metrics endpoint (embedded server without prometheus-cpp) |
| **Raw TCP** | protocol | Raw protocol over TCP/TLS |
| **WebSocket** | protocol | ws:// / wss:// streaming messages |
| **Kubernetes** | deploy | Helm charts, service monitors |
| **Docker** | deploy | Multi-stage build, Ubuntu 24.04, non-root user |

### Tech stack

**C++ Core:** C++20, Boost.Beast/ASIO, OpenSSL, yaml-cpp, nlohmann_json, Prometheus-cpp (optional)

**Testing:** GoogleTest (189 tests, 21 suite executables)

**CI/CD:** GitHub Actions, AddressSanitizer, ThreadSanitizer, clang-tidy, codecov, lcov

**Infrastructure:** Docker multi-stage, Kubernetes/Helm, Prometheus + Grafana, Jaeger

**Python:** pybind11, setuptools, mypy, black

### CI Pipeline

| Job | Purpose | Status |
|-----|---------|--------|
| **Build & Test** | Release build + GTest (189 tests, 15 suites) | ✅ |
| **Coverage** | Debug + --coverage + codecov | ✅ |
| **AddressSanitizer** | ASan + UBSan, g++-13 | ✅ |
| **ThreadSanitizer** | TSan, g++-13 | ✅ |
| **Performance Regression** | Benchmarks + regression gate vs baseline | ✅ |
| **Integration** | Mock server, connection pool, HTTP client, OAuth2, Vault | ✅ |
| **E2E Smoke** | Demo services + CLI, SLA validation | ✅ |
| **Lint** | clang-tidy, black, flake8, mypy, YAML validation | ✅ |

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

### License

Apache 2.0 — see [LICENSE](LICENSE).

---

<a id="русский"></a>
## Русский

Нагрузочное тестирование HTTP-сервисов. Ядро на C++20 (Boost.Beast/ASIO), YAML-сценарии,
интеграция с Vault, OAuth2, OpenTelemetry и Prometheus.

### Возможности

| Возможность | Статус | Детали |
|------------|--------|--------|
| **Async HTTP/1.1 Client** | готово | Boost.Beast + ASIO, per-request safety, URL encoding |
| **Raw TCP Client** | готово | Произвольные байты поверх TCP/TLS |
| **WebSocket Client** | готово | ws:// + wss://, произвольные сообщения |
| **Protocol Factory** | готово | Кастомные протоколы через `register_protocol()` |
| **TokenBucket Rate Limiter** | готово | Контроль RPS, consume/try_consume, overflow-safe |
| **Connection Pool** | готово | acquire/release, idle cleanup, stats |
| **YAML Scenario Engine** | готово | yaml-cpp, env vars `${VAR:-default}`, SLA валидация |
| **OAuth2 Client Credentials** | готово | HTTP POST, JSON, auto-refresh, URL encoding |
| **HashiCorp Vault** | готово | KV v2, AppRole, database creds, health check, path sanitization |
| **mTLS** | готово | Взаимная TLS аутентификация |
| **TLS Context** | готово | Центральная настройка TLS для исходящих соединений |
| **OpenTelemetry OTLP** | готово | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus Exporter** | готово¹ | /metrics endpoint, counters, histograms, gauges |
| **CLI Tool** | готово | Запуск нагрузки из командной строки |
| **HTTP Worker** | готово | Воркер без YAML, только аргументы CLI |
| **Helm Charts** | готово | K8s деплой |
| **Docker Multi-stage** | готово | Multi-stage runtime image, Ubuntu 24.04, non-root user |
| **Python SDK** | готово | urllib-based (pure Python), 100% coverage, 143 tests |
| **gRPC Worker** | готово² | Управление нагрузкой через gRPC control plane |

> ¹ Полные counters/histograms требуют `prometheus-cpp` при сборке. Без него — встроенный HTTP сервер на Boost.Beast (текстовый /metrics endpoint).
> ² Требуется `gRPC` и `Protobuf` при сборке (автообнаружение).

### Быстрый старт

#### Bash (системные пакеты, без Conan)

```bash
# Ubuntu 24.04
sudo apt-get install -y cmake ninja-build g++-13 \
  libboost-all-dev libssl-dev libyaml-cpp-dev

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVOLLEY_BUILD_TESTS=ON \
  -DVOLLEY_BUILD_TOOLS=ON

cmake --build build
cd build && ctest --output-on-failure
```

#### Docker (Conan-based)

```bash
docker build -t volley:latest -f deploy/docker/Dockerfile .
docker run --rm volley:latest --help
```

#### Запуск нагрузочного теста

```bash
# Из YAML сценария
./build/tools/volley \
  --config=scenarios/ecommerce/load-test.yaml \
  --rps=5000 \
  --duration=300

# С OAuth2
./build/tools/volley \
  --config=test.yaml \
  --auth-type=oauth2 \
  --client-id=$CLIENT_ID \
  --client-secret=$CLIENT_SECRET \
  --token-endpoint=https://auth.company.com/oauth/token

# С Vault
./build/tools/volley \
  --config=test.yaml \
  --vault-addr=https://vault.company.com:8200 \
  --vault-token=$VAULT_TOKEN

# С OTLP трейсингом
./build/tools/volley \
  --config=test.yaml \
  --otlp-endpoint=http://jaeger:4318
```

### HTTP Worker (без YAML, только CLI аргументы)

```bash
./build/workers/http_worker/http_worker \
  --host=api.target.com \
  --port=443 \
  --path=/api/v1/health \
  --method=GET \
  --rps=1000 \
  --duration=60
```

### Структура проекта

```
volley/
├── core/                          # C++20 ядро
│   ├── net/                       # HTTP клиент + connection pool
│   ├── metrics/                   # Lock-free сбор метрик + Prometheus
│   ├── security/                  # OAuth2, API Key, mTLS, TLS Context
│   ├── vault/                     # HashiCorp Vault HTTP клиент
│   ├── otel/                      # OpenTelemetry OTLP/HTTP+JSON экспорт
│   ├── scenario/                  # YAML engine + SLA валидация
│   └── token_bucket.cpp           # Rate limiter (thread-safe)
├── include/cppload/               # Публичные заголовки (API)
├── workers/                       # Исполняемые воркеры
│   ├── http_worker/               # Автономный HTTP воркер
│   └── grpc_worker/               # gRPC control plane (опционально)
├── tools/                         # CLI утилита (volley)
├── tests/                         # GTest (21 exe, 189 тестов)
├── python/                        # Python SDK (pure Python, alpha)
├── deploy/                        # Docker, Helm, demo-env
│   ├── docker/                    # Multi-stage Dockerfile
│   ├── kubernetes/helm/           # Helm charts
│   ├── grafana/                   # Dashboards
│   └── demo-env/                  # Docker Compose dev окружение
├── scenarios/                     # Библиотека сценариев
├── cmake/                         # CMake конфигурация
│   └── volley-config.cmake.in     # find_package() support
├── docs/                          # ADR, архитектура
├── proto/                         # Protobuf спецификации (gRPC)
├── .github/workflows/             # CI (8 jobs)
├── CMakePresets.json              # CMake presets
├── conanfile.py                   # Conan 2.0 рецепт
└── VERSION                        # Single source of truth
```

### Тестирование

#### C++ Tests

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVOLLEY_BUILD_TESTS=ON

cmake --build build
cd build && ctest --output-on-failure
```

#### Python Tests

```bash
cd python
pip install -e .
pytest --cov=volley --cov-report=term-missing
pytest tests/test_integration_*.py -v
```

**Python SDK:** 143 теста, 100% coverage (460/460 statements)

### Интеграции

| Система | Тип | Статус |
|---------|-----|--------|
| **HashiCorp Vault** | secrets | KV v2 read/write, AppRole login, database creds, health check |
| **OAuth2** | auth | client_credentials grant, auto-refresh, URL encoding |
| **mTLS** | auth | Взаимная аутентификация через TLS сертификаты |
| **OpenTelemetry** | tracing | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus** | metrics | /metrics endpoint (embedded server без prometheus-cpp) |
| **Raw TCP** | protocol | Сырой протокол поверх TCP/TLS |
| **WebSocket** | protocol | ws:// / wss:// потоковые сообщения |
| **Kubernetes** | deploy | Helm charts, service monitors |
| **Docker** | deploy | Multi-stage build, Ubuntu 24.04, non-root user |

### Контрибьюция

См. [CONTRIBUTING.md](CONTRIBUTING.md).

Проект использует:
- **pre-commit** хуки для форматирования и линтинга
- **Conventional Commits** для именования коммитов
- **ADR** для архитектурных решений (`docs/adr/`)

### Лицензия

Apache 2.0 — см. [LICENSE](LICENSE).

### Автор

**ssrjkk** — [@ssrjkk](https://t.me/ssrjkk), ray013lefe@gmail.com
