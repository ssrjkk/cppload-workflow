# volley

[![CI](https://github.com/ssrjkk/volley/actions/workflows/ci.yml/badge.svg)](https://github.com/ssrjkk/volley/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/ssrjkk/volley/branch/main/graph/badge.svg)](https://codecov.io/gh/ssrjkk/volley)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

**[English](README.md)**

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
