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
| Сложный деплой в K8s | Helm charts + multi-stage Docker <50MB |
| Конфиги в XML/JSON | YAML сценарии с env-подстановкой `${VAR:-default}` |
| Нет SLA валидации | Встроенная проверка error_rate + p99 latency |
| Безопасность | mTLS, TLS Context, API Key, Bearer Token |

## Возможности

| Возможность | Статус | Детали |
|------------|--------|--------|
| **Async HTTP/1.1 Client** | ✅ PROD | Boost.Beast + ASIO, per-request safety, URL encoding |
| **TokenBucket Rate Limiter** | ✅ PROD | Точный контроль RPS, consume/try_consume, overflow-safe |
| **Connection Pool** | ✅ PROD | Переиспользование TCP/TLS соединений |
| **YAML Scenario Engine** | ✅ PROD | yaml-cpp парсер, env vars `${VAR:-default}`, SLA валидация |
| **OAuth2 Client Credentials** | ✅ PROD | HTTP POST, JSON парсинг, auto-refresh, URL encoding |
| **HashiCorp Vault** | ✅ PROD | KV v2, AppRole, database creds, health check, path sanitization |
| **mTLS** | ✅ PROD | Взаимная TLS аутентификация с сертификатами |
| **TLS Context** | ✅ PROD | Централизованная настройка TLS для всех outbound соединений |
| **OpenTelemetry OTLP** | ✅ PROD | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus Exporter** | ✅ PROD | /metrics endpoint, counters, histograms, gauges |
| **CLI Tool** | ✅ PROD | Полноценный запуск нагрузки из командной строки |
| **HTTP Worker** | ✅ PROD | Автономный воркер без YAML, только аргументы CLI |
| **Helm Charts** | ✅ PROD | K8s деплой за 2 минуты |
| **Docker Multi-stage** | ✅ PROD | <50MB runtime image, Ubuntu 22.04, non-root user |
| **AddressSanitizer** | ✅ CI | Каждый коммит проверяется на memory errors |
| **clang-tidy Lint** | ✅ CI | Статический анализ C++ кода |
| **Python SDK** | 🔶 ALPHA | pybind11 биндинги, Python-контроллеры |
| **gRPC Worker** | 🔧 PLANNED | Управление нагрузкой через gRPC control plane |

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
│  │  Scenario Engine                                  │    │
│  │  • YAML парсер (env vars, load profiles, SLA)     │    │
│  │  • TokenBucket rate limiter                       │    │
│  │  • Step callback pipeline                         │    │
│  └────────────────────┬─────────────────────────────┘    │
│                       │                                   │
│  ┌────────────────────▼─────────────────────────────┐    │
│  │  HTTP Client (Boost.Beast + ASIO)                │    │
│  │  • Async request/resolve/connect                 │    │
│  │  • Connection pool (reuse TCP/TLS)               │    │
│  │  • mTLS / TLS Context                            │    │
│  │  • URL encoding + CR/LF sanitization             │    │
│  └────────────────────┬─────────────────────────────┘    │
│                       │                                   │
│  ┌────────────────────▼─────────────────────────────┐    │
│  │  Metrics & Observability                         │    │
│  │  • Lock-free Metrics Collector (p50/p95/p99)     │    │
│  │  • OTLP/HTTP+JSON Exporter (thread-safe)         │    │
│  │  • Prometheus /metrics экспорт                   │    │
│  └──────────────────────────────────────────────────┘    │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  Auth & Secrets                                   │    │
│  │  • OAuth2 (client_credentials, auto-refresh)     │    │
│  │  • API Key / Bearer Token                        │    │
│  │  • mTLS (взаимная TLS аутентификация)           │    │
│  │  • HashiCorp Vault (KV v2, AppRole, DB creds)   │    │
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
  max_error_rate: 0.1
  max_p99_latency: 500ms
```

## Интеграции

| Система | Тип | Статус |
|---------|-----|--------|
| **HashiCorp Vault** | secrets | KV v2 read/write, AppRole login, database creds, health check |
| **OAuth2** | auth | client_credentials grant, auto-refresh, URL encoding |
| **mTLS** | auth | Взаимная аутентификация через TLS сертификаты |
| **OpenTelemetry** | tracing | OTLP/HTTP+JSON, batch export, sampling, thread-safe |
| **Prometheus** | metrics | /metrics endpoint, histograms, gauges, counters |
| **Kubernetes** | deploy | Helm charts, service monitors |
| **Docker** | deploy | Multi-stage build, <50MB, non-root user |

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
│   └── grpc_worker/               # gRPC control plane (🔧 PLANNED)
├── tools/                         # CLI утилита (cppload-cli)
├── tests/                         # GTest (8 test suites, 51+ тестов)
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
├── .github/workflows/             # CI (4 jobs: build, coverage, asan, lint)
├── .pre-commit-config.yaml        # Pre-commit хуки
├── CMakePresets.json              # CMake presets (CMake 3.21+)
├── conanfile.py                   # Conan 2.0 рецепт
└── VERSION                        # Single source of truth (1.0.0)
```

## Технологический стек

**C++ Core:** C++20, Boost.Beast/ASIO, OpenSSL, yaml-cpp, nlohmann_json, Prometheus-cpp (optional)

**Testing:** GoogleTest (51+ тестов, 8 test suites)

**CI/CD:** GitHub Actions, AddressSanitizer, clang-tidy, codecov, lcov

**Инфраструктура:** Docker multi-stage (<50MB), Kubernetes/Helm, Prometheus + Grafana, Jaeger

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
| test_yaml_parser.cpp | 10 | Парсинг конфигов, env vars, SLA, error handling |
| test_metrics.cpp | 9 | Snapshot, percentiles (p50/p95/p99), reset, RPS |
| test_token_bucket.cpp | 7 | Consume, try_consume, concurrent, rate/burst, invalid rate |
| test_vault.cpp | 6 | KV v2, AppRole, database creds, health, error handling, path sanitization |
| test_otlp.cpp | 6 | Span lifecycle, attributes, trace_id, batch export |
| test_auth.cpp | 5 | OAuth2, API Key, Bearer, mTLS |
| test_http_client.cpp | 4 | Async request, timeout, keep-alive, graceful failure |
| test_prometheus.cpp | 4 | Metrics registry, counters, histograms, gauges |

## Benchmarks

**Целевые показатели:** 50k+ RPS на инстанс (зависит от целевого сервиса и сетевой задержки).

```bash
cmake --build build --target benchmark
cd build && ./tests/benchmarks/benchmark_http_client
```

## CI Pipeline

| Job | Назначение | Статус |
|-----|-----------|--------|
| **Build & Test** | Сборка Release + GTest (8 suites, 51+ тестов) | ✅ |
| **Coverage** | Debug + --coverage + codecov | ✅ |
| **AddressSanitizer** | ASan + UBSan, g++-13 | ✅ |
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
