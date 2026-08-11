# cppload-pro

[![CI](https://github.com/ssrjkk/cppload-workflow/actions/workflows/ci.yml/badge.svg)](https://github.com/ssrjkk/cppload-workflow/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/ssrjkk/cppload-workflow/branch/main/graph/badge.svg)](https://codecov.io/gh/ssrjkk/cppload-workflow)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

**Enterprise Load Testing Platform** — высокопроизводительная система нагрузочного тестирования
с ядром на C++20. 50k+ RPS на инстанс, интеграция с Vault, OAuth2, OpenTelemetry, Prometheus и Kubernetes.

---

## Зачем cppload-pro?

| Проблема | Решение cppload-pro |
|----------|---------------------|
| JMeter/Gatling не тянут высокие RPS | C++20 ядро на Boost.Beast/ASIO — 50k+ RPS на одной ноде |
| Нет интеграции с корпоративным Vault | Встроенный Vault HTTP клиент: KV v2, AppRole, dynamic database credentials |
| OAuth2 токены протухают посреди теста | client_credentials grant с автоматическим refresh |
| Нет observability | OTLP/HTTP+JSON трассировка + Prometheus /metrics endpoint |
| Сложный деплой в K8s | Helm charts + multi-stage Docker |
| Конфиги в XML/JSON | YAML сценарии с env-подстановкой `${VAR:-default}` |
| Нет SLA валидации | Встроенная проверка error_rate + p99 latency |
| Безопасность | mTLS, TLS Context, API Key, Bearer Token |

## Возможности

| Возможность | Статус | Детали |
|------------|--------|--------|
| **Async HTTP/1.1 Client** | ✅ PROD | Boost.Beast + ASIO, per-request safety, URL encoding |
| **Raw TCP Client** | ✅ PROD | Произвольные байты поверх TCP/TLS, любой протокол вручную |
| **WebSocket Client** | ✅ PROD | ws:// + wss://, произвольные сообщения |
| **Protocol Factory** | ✅ PROD | Регистрация кастомных протоколов через `register_protocol()` |
| **TokenBucket Rate Limiter** | ✅ PROD | Точный контроль RPS, consume/try_consume, overflow-safe |
| **Connection Pool** | ✅ PROD | Пул клиентских объектов: acquire/release, idle cleanup, stats |
| **YAML Scenario Engine** | ✅ PROD | yaml-cpp парсер, env vars `${VAR:-default}`, SLA валидация |
| **OAuth2 Client Credentials** | ✅ PROD | HTTP POST, JSON парсинг, auto-refresh, URL encoding |
| **HashiCorp Vault** | ✅ PROD | KV v2, AppRole, database creds, health check, path sanitization |
| **mTLS** | ✅ PROD | Взаимная TLS аутентификация с сертификатами |
| **TLS Context** | ✅ PROD | Централизованная настройка TLS для всех outbound соединений |
| **OpenTelemetry OTLP** | ✅ PROD | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus Exporter** | ✅ PROD¹ | /metrics endpoint, counters, histograms, gauges |
| **CLI Tool** | ✅ PROD | Полноценный запуск нагрузки из командной строки |
| **HTTP Worker** | ✅ PROD | Автономный воркер без YAML, только аргументы CLI |
| **Helm Charts** | ✅ PROD | K8s деплой за 2 минуты |
| **Docker Multi-stage** | ✅ PROD | Multi-stage runtime image, Ubuntu 26.04, non-root user |
| **AddressSanitizer** | ✅ CI | Каждый коммит проверяется на memory errors |
| **clang-tidy Lint** | ✅ CI | Статический анализ C++ кода |
| **Python SDK** | 🔶 ALPHA | urllib-based (pure Python), отдельные pybind11 биндинги |
| **gRPC Worker** | ✅ PROD² | Управление нагрузкой через gRPC control plane |

> ¹ Полные counters/histograms требуют `prometheus-cpp` при сборке. Без него — встроенный HTTP сервер на Boost.Beast (текстовый /metrics endpoint).
> ² Требуется `gRPC` и `Protobuf` при сборке (автообнаружение).

## Быстрый старт

### Bash (системные пакеты, без Conan)

```bash
# Ubuntu 24.04
sudo apt-get install -y cmake ninja-build g++-13 \
  libboost-all-dev libssl-dev libyaml-cpp-dev

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPLOAD_BUILD_TESTS=ON \
  -DCPLOAD_BUILD_TOOLS=ON

cmake --build build
cd build && ctest --output-on-failure
```

### Docker (Conan-based)

```bash
docker build -t cppload-pro:latest -f deploy/docker/Dockerfile .
docker run --rm cppload-pro:latest --help
```

### Запуск нагрузочного теста

```bash
# Из YAML сценария
./build/tools/cppload-cli \
  --config=scenarios/ecommerce/load-test.yaml \
  --rps=5000 \
  --duration=300

# С OAuth2
./build/tools/cppload-cli \
  --config=test.yaml \
  --auth-type=oauth2 \
  --client-id=$CLIENT_ID \
  --client-secret=$CLIENT_SECRET \
  --token-endpoint=https://auth.company.com/oauth/token

# С Vault
./build/tools/cppload-cli \
  --config=test.yaml \
  --vault-addr=https://vault.company.com:8200 \
  --vault-token=$VAULT_TOKEN

# С OTLP трейсингом
./build/tools/cppload-cli \
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

## Архитектура

```
┌──────────────────────────────────────────────────────────┐
│  CLI Tool / HTTP Worker / Python SDK                     │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Scenario Engine                                 │    │
│  │  • YAML парсер (env vars, load profiles, SLA)    │    │
│  │  • TokenBucket rate limiter                      │    │
│  │  • Step callback pipeline                        │    │
│  └────────────────────┬─────────────────────────────┘    │
│                       │                                  │
│  ┌────────────────────▼─────────────────────────────┐    │
│  │  HTTP Client (Boost.Beast + ASIO)                │    │
│  │  • Async request/resolve/connect                 │    │
│  │  • Connection pool (acquire/release client handles) │   │
│  │  • mTLS / TLS Context                            │    │
│  │  • URL encoding + CR/LF sanitization             │
│  │  • Raw TCP (произвольные байты)                  │
│  │  • WebSocket (ws:// + wss://)                    │
│  │  • Protocol Factory (регистрация кастомных)       │    │
│  └────────────────────┬─────────────────────────────┘    │
│                       │                                  │
│  ┌────────────────────▼─────────────────────────────┐    │
│  │  Metrics & Observability                         │    │
│  │  • Lock-free Metrics Collector (p50/p95/p99)     │    │
│  │  • OTLP/HTTP+JSON Exporter (thread-safe)         │    │
│  │  • Prometheus /metrics экспорт                   │    │
│  └──────────────────────────────────────────────────┘    │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Auth & Secrets                                  │    │
│  │  • OAuth2 (client_credentials, auto-refresh)     │    │
│  │  • API Key / Bearer Token                        │    │
│  │  • mTLS (взаимная TLS аутентификация)            │    │
│  │  • HashiCorp Vault (KV v2, AppRole, DB creds)    │    │
│  └──────────────────────────────────────────────────┘    │
└─────────────────────────┬────────────────────────────────┘
                          │
                          ▼
                 [Target HTTP Service]
```

## Пример YAML сценария

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

## Интеграции

| Система | Тип | Статус |
|---------|-----|--------|
| **HashiCorp Vault** | secrets | KV v2 read/write, AppRole login, database creds, health check |
| **OAuth2** | auth | client_credentials grant, auto-refresh, URL encoding |
| **mTLS** | auth | Взаимная аутентификация через TLS сертификаты |
| **OpenTelemetry** | tracing | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus** | metrics | /metrics endpoint (embedded server без prometheus-cpp) |
| **Raw TCP** | protocol | Любой сырой протокол поверх TCP/TLS |
| **WebSocket** | protocol | ws:// / wss:// потоковые сообщения |
| **Kubernetes** | deploy | Helm charts, service monitors |
| **Docker** | deploy | Multi-stage build, Ubuntu 26.04, non-root user |

## Структура проекта

```
cppload-pro/
├── core/                          # C++20 ядро
│   ├── net/                       # HTTP клиент + connection pool
│   ├── metrics/                   # Lock-free сбор метрик + Prometheus
│   ├── security/                  # OAuth2, API Key, mTLS, TLS Context
│   ├── vault/                     # HashiCorp Vault HTTP клиент
│   ├── otel/                      # OpenTelemetry OTLP/HTTP+JSON экспорт
│   ├── scenario/                  # YAML engine + SLA валидация
│   └── token_bucket.cpp           # Rate limiter (thread-safe)
├── include/cppload/               # Публичные заголовки (API)
│   ├── core/                      # token_bucket.hpp
│   ├── metrics/                   # collector.hpp, prometheus_exporter.hpp
│   ├── net/                       # http_client.hpp, connection_pool.hpp
│   ├── otel/                      # exporter.hpp (TraceConfig, Tracer)
│   ├── scenario/                  # engine.hpp (ScenarioEngine)
│   ├── security/                  # auth_provider.hpp, tls_context.hpp
│   └── vault/                     # vault_client.hpp
├── workers/                       # Исполняемые воркеры
│   ├── http_worker/               # Автономный HTTP воркер (✅ PROD)
│   └── grpc_worker/               # gRPC control plane (✅ PROD, опционально)
├── tools/                         # CLI утилита (cppload-cli)
├── tests/                         # GTest (21 exe, 24 suites, 182 теста)
├── python/                        # Python SDK (pybind11, alpha)
├── deploy/                        # Docker, Helm, demo-env
│   ├── docker/                    # Multi-stage Dockerfile
│   ├── kubernetes/helm/           # Helm charts
│   ├── grafana/                   # Dashboards
│   └── demo-env/                  # Docker Compose dev окружение
├── scenarios/                     # Библиотека сценариев
│   ├── ecommerce/                 # E-commerce flow
│   ├── auth/                      # Auth flow
│   └── spike/                     # Spike testing
├── cmake/                         # CMake конфигурация
│   └── cppload-config.cmake.in    # find_package() support
├── docs/                          # ADR, архитектура
├── proto/                         # Protobuf спецификации (gRPC)
│   └── load_controller.proto
├── .github/workflows/             # CI (6 jobs: build, coverage, asan, benchmark, integration, lint)
├── .pre-commit-config.yaml        # Pre-commit хуки
├── CMakePresets.json              # CMake presets (CMake 3.21+)
├── conanfile.py                   # Conan 2.0 рецепт
└── VERSION                        # Single source of truth (1.0.0)
```

## Технологический стек

**C++ Core:** C++20, Boost.Beast/ASIO, OpenSSL, yaml-cpp, nlohmann_json, Prometheus-cpp (optional)

**Testing:** GoogleTest (182 теста, 24 test suites)

**CI/CD:** GitHub Actions, AddressSanitizer, clang-tidy, codecov, lcov

**Инфраструктура:** Docker multi-stage, Kubernetes/Helm, Prometheus + Grafana, Jaeger

**Python:** pybind11, setuptools, mypy, black

## Тестирование

```bash
# Сборка с тестами
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCPLOAD_BUILD_TESTS=ON

cmake --build build

# Запуск всех тестов
cd build && ctest --output-on-failure

# Запуск конкретного test suite
./tests/test_metrics
./tests/test_token_bucket
```

### Test Suites

| Файл | Тестов | Что проверяет |
|------|--------|---------------|
| test_yaml_parser.cpp | 13 | Парсинг конфигов, env vars, SLA, assertions, error handling |
| test_metrics.cpp | 13 | Snapshot, percentiles (p50/p95/p99), reset, RPS |
| test_metrics_stress.cpp | 4 | Нагрузочный сценарий на collector |
| test_token_bucket.cpp | 14 | Consume, try_consume, concurrent, rate/burst, invalid rate |
| test_result.cpp | 25 | Result API, or_else, and_then, move, chaining, void-returning |
| test_url_parse.cpp | 21 | URL parsing, edge cases, URL encoding |
| test_vault.cpp | 9 | KV v2, AppRole, database creds, health, error handling, path sanitization |
| test_otlp.cpp | 6 | Span lifecycle, attributes, trace_id, batch export |
| test_auth.cpp | 6 | OAuth2, API Key, Bearer, mTLS |
| test_http_client.cpp | 4 | Async request, timeout, keep-alive, graceful failure |
| test_prometheus.cpp | 4 | Metrics registry, counters, histograms, gauges |
| test_tls_context.cpp | 7 | TLS context creation, version validation, cert loading |
| test_protocol_factory.cpp | 8 | Protocol registration, HTTP/TCP/WebSocket creation |
| test_io_context_pool.cpp | 8 | Pool initialization, context distribution, thread safety |
| test_sharded_metrics.cpp | 13 | Sharded collection, percentiles, concurrent, reset |
| test_connection_pool_integration.cpp | 6 | Connection pool: acquire/release, reuse, max limits, idle cleanup, host isolation, concurrency |
| test_engine_integration.cpp | 1 | Scenario engine: sustained load через полную стадию сценария |
| test_http_client_integration.cpp | 4 | HTTP: success, POST body, IPv6 literal host header, server error |
| test_oauth2_integration.cpp | 4 | OAuth2: token fetch, auto-refresh on expiry, server error, static bearer |
| test_tls_integration.cpp | 7 | TLS: hostname/IP matching, verify modes, raw TCP, SSL connector, WebSocket over TLS |
| test_vault_integration.cpp | 5 | Vault: KV v2 read/map/put, AppRole token, database creds |

## Benchmarks

**Целевые показатели:** 50k+ RPS на инстанс (зависит от целевого сервиса и сетевой задержки).

```bash
cmake --build build --target http_client_bench
cd build && ./tests/benchmarks/http_client_bench
```

## CI Pipeline

| Job | Назначение | Статус |
|-----|-----------|--------|
| **Build & Test** | Сборка Release + GTest (24 suites, 182 теста) | ✅ |
| **Coverage** | Debug + --coverage + codecov | ✅ |
| **AddressSanitizer** | ASan + UBSan, g++-13 | ✅ |
| **Benchmark** | Сборка + прогон бенчмарков (google benchmark) | ✅ |
| **Integration** | Мок-сервер, connection pool, HTTP client, OAuth2, Vault | ✅ |
| **Lint** | clang-tidy, black, flake8, YAML валидация | ✅ |

## Контрибьюция

См. [CONTRIBUTING.md](CONTRIBUTING.md).

Проект использует:
- **pre-commit** хуки для форматирования и линтинга
- **Conventional Commits** для именования коммитов
- **ADR** для архитектурных решений (`docs/adr/`)

## Лицензия

Apache 2.0 — см. [LICENSE](LICENSE).

## Автор

**ssrjkk** — [@ssrjkk](https://t.me/ssrjkk), ray013lefe@gmail.com
