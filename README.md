# cppload-pro

[![CI](https://github.com/ssrjkk/cppload-workflow/actions/workflows/ci.yml/badge.svg)](https://github.com/ssrjkk/cppload-workflow/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/ssrjkk/cppload-workflow/branch/main/graph/badge.svg)](https://codecov.io/gh/ssrjkk/cppload-workflow)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

**Enterprise Load Testing Platform** — высокопроизводительная система нагрузочного тестирования с ядром на C++20, готовая к использованию в корпоративной инфраструктуре. 50k+ RPS на инстанс, интеграция с Vault, OAuth2, OpenTelemetry и Kubernetes.

---

## Зачем cppload-pro?

| Проблема | Решение cppload-pro |
|----------|-------------------|
| JMeter/Gatling «не тянут» высокие RPS | C++20 ядро на Boost.Beast — 50k+ RPS на одной ноде |
| Нет интеграции с корпоративным Vault | Встроенный Vault HTTP клиент (KV v2, AppRole, dynamic secrets) |
| OAuth2 токены протухают посреди теста | Автоматический refresh с client_credentials grant |
| Нет observability | OTLP/HTTP+JSON трассировка + Prometheus метрики |
| Сложный деплой в K8s | Helm charts + multi-stage Docker <50MB |
| Конфиги в XML/JSON | YAML сценарии с env-подстановкой `${VAR:-default}` |
| Нет SLA валидации | Встроенная проверка error_rate + p99 latency |

## Возможности

| Возможность | Статус | Детали |
|------------|--------|--------|
| **Async HTTP/1.1 Client** | ✅ PROD | Boost.Beast + ASIO, lock-free метрики |
| **TokenBucket Rate Limiter** | ✅ PROD | Точный контроль RPS, consume/try_consume |
| **Connection Pool** | ✅ PROD | Переиспользование TCP/TLS соединений |
| **YAML Scenario Engine** | ✅ PROD | yaml-cpp парсер, env vars, SLA валидация |
| **OAuth2 Client Credentials** | ✅ PROD | Реальный HTTP POST, JSON парсинг, auto-refresh |
| **HashiCorp Vault** | ✅ PROD | KV v2, AppRole, database creds, health check |
| **OpenTelemetry OTLP** | ✅ PROD | OTLP/HTTP+JSON, batch export, sampling |
| **Prometheus Exporter** | ✅ PROD | /metrics endpoint с метриками |
| **CLI Tool** | ✅ PROD | Полноценный запуск нагрузки из командной строки |
| **Helm Charts** | ✅ PROD | K8s деплой за 2 минуты |
| **Docker Multi-stage** | ✅ PROD | <50MB runtime image |
| **AddressSanitizer** | ✅ CI | Каждый коммит проверяется на memory errors |
| **clang-tidy Lint** | ✅ CI | Статический анализ C++ кода |

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
┌─────────────────────────────────────────────────┐
│  CLI / HTTP Worker                              │
│  • TokenBucket rate limiter                     │
│  • Boost.Beast HTTP Client                      │
│  • Lock-free Metrics Collector                  │
│  • OpenTelemetry Exporter (OTLP/HTTP+JSON)      │
│  • Auth: OAuth2, API Key, Bearer, mTLS          │
│  • Secrets: HashiCorp Vault (KV v2, AppRole)    │
└────────────────────┬────────────────────────────┘
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
  protocol: http2

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
          body:
            item_id: "${faker:uuid}"
          assertions:
            - status_code == 201

sla:
  error_rate: "< 0.1%"
  p99_latency: "< 500ms"
```

## Интеграции

| Система | Тип | Статус |
|---------|-----|--------|
| **HashiCorp Vault** | secrets | KV v2 read/write, AppRole login, database creds |
| **OAuth2** | auth | client_credentials grant, auto-refresh |
| **OpenTelemetry** | tracing | OTLP/HTTP+JSON, batch export, sampling |
| **Prometheus** | metrics | /metrics endpoint, histograms, gauges |
| **Kubernetes** | deploy | Helm charts, service monitors |
| **Docker** | deploy | Multi-stage build, <50MB |

## Структура проекта

```
cppload-pro/
├── core/                    # C++20 ядро
│   ├── net/                 # HTTP клиент + connection pool
│   ├── metrics/             # Lock-free сбор метрик
│   ├── security/            # OAuth2, API Key, mTLS
│   ├── vault/               # HashiCorp Vault HTTP клиент
│   ├── otel/                # OpenTelemetry OTLP экспорт
│   ├── scenario/            # YAML engine + SLA валидация
│   └── token_bucket.cpp     # Rate limiter
├── workers/                 # Исполняемые воркеры
├── tools/                   # CLI утилита
├── tests/                   # GTest тесты (33+ тестов)
├── deploy/                  # Docker, Helm, demo-env
├── scenarios/               # Библиотека сценариев
└── docs/                    # ADR, спецификации
```

## Технологический стек

**C++ Core:** C++20, Boost.Beast/ASIO, OpenSSL, yaml-cpp, nlohmann_json, Prometheus-cpp

**CI/CD:** GitHub Actions, AddressSanitizer, clang-tidy, codecov, lcov

**Инфраструктура:** Docker multi-stage (<50MB), Kubernetes/Helm, Prometheus + Grafana, Jaeger

## Benchmarks

**Целевые показатели:** 50k+ RPS на инстанс (зависит от целевого сервиса и сетевой задержки).

```bash
cmake --build build --target benchmark
cd build && ./tests/benchmarks/benchmark_http_client
```

## CI Pipeline

| Job | Назначение | Статус |
|-----|-----------|--------|
| **Build & Test** | Сборка Release + GTest | ✅ |
| **Coverage** | Debug + --coverage + codecov | ✅ |
| **AddressSanitizer** | ASan + UBSan, g++-13 | ✅ |
| **Lint** | clang-tidy, black, flake8, YAML | ✅ |

## Лицензия

Apache 2.0 — см. [LICENSE](LICENSE).

## Автор

**ssrjkk** — [@ssrjkk](https://t.me/ssrjkk), ray013lefe@gmail.com
