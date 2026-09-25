# volley

**Высокопроизводительное нагрузочное тестирование HTTP-сервисов**

[![CI](https://github.com/ssrjkk/volley/actions/workflows/ci.yml/badge.svg)](https://github.com/ssrjkk/volley/actions/workflows/ci.yml)
[![Coverage](https://codecov.io/gh/ssrjkk/volley/branch/main/graph/badge.svg)](https://codecov.io/gh/ssrjkk/volley)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support/20)

[English](README.md)

---

Enterprise-grade нагрузочное тестирование на C++20 async ядре. YAML-сценарии, безопасность OAuth2/Vault/mTLS, полная наблюдаемость с OpenTelemetry и Prometheus.

## Почему volley?

- **Молниеносная скорость** — Lock-free метрики, async I/O на Boost.Beast/ASIO, пул соединений
- **Production-ready** — 8-job CI с ASan/TSan, 100% покрытие тестами, ноль предупреждений
- **Гибкость** — HTTP/1.1, WebSocket, raw TCP, кастомные протоколы через factory pattern
- **Наблюдаемость** — OTLP/HTTP+JSON трейсинг, Prometheus /metrics, валидация SLA
- **Безопасность** — OAuth2, HashiCorp Vault, mTLS, API keys из коробки

## Быстрый старт

### Сборка из исходников

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

### Запуск первого теста

```bash
./build/tools/volley \
  --config=scenarios/ecommerce/load-test.yaml \
  --rps=5000 \
  --duration=300
```

## Пример сценария

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

## Архитектура

```
┌──────────────────────────────────────────────────────────┐
│  CLI Tool / Control Plane REST API / Python SDK          │
├──────────────────────────────────────────────────────────┤
│  Scenario Engine                                         │
│  • YAML парсер с env vars и load profiles               │
│  • TokenBucket rate limiter (thread-safe)               │
│  • SLA валидация и step callbacks                       │
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

### Control Plane

C++20 REST API сервер (`volley-control-plane`) для управления проектами, сценариями, запусками и SLA-политиками. Построен на Boost.Beast, чистая архитектура (Repository pattern), in-memory хранилище.

## Возможности

| Категория | Функционал |
|-----------|------------|
| **Протоколы** | HTTP/1.1, WebSocket (ws/wss), Raw TCP/TLS, кастомные через `register_protocol()` |
| **Безопасность** | OAuth2 client_credentials, HashiCorp Vault KV v2, mTLS, API keys, Bearer tokens |
| **Наблюдаемость** | OpenTelemetry OTLP/HTTP+JSON, Prometheus counters/histograms, SLA валидация |
| **Производительность** | Lock-free метрики, пул соединений, TokenBucket rate limiting, async I/O |
| **Деплой** | Docker multi-stage, Kubernetes Helm charts, Prometheus + Grafana дашборды |
| **Control Plane** | C++20 REST API (Boost.Beast), управление проектами/сценариями/запусками, оценка SLA-политик |

## Технологический стек

**Ядро:** C++20, Boost.Beast/ASIO, OpenSSL, yaml-cpp, nlohmann_json  
**Тестирование:** GoogleTest (201 тест, 19 suite), 100% покрытие Python SDK  
**CI/CD:** GitHub Actions (8 jobs), ASan/TSan, clang-tidy, codecov  
**Инфра:** Docker, Kubernetes/Helm, Prometheus, Grafana, Jaeger  
**SDK:** Python (pure Python, urllib-based, 143 теста)

## CI Pipeline

Каждый коммит проходит через 8 CI jobs:

- **Build & Test** — Release сборка + 201 GTest тестов
- **Coverage** — Debug + --coverage + codecov upload
- **AddressSanitizer** — ASan + UBSan проверка памяти
- **ThreadSanitizer** — TSan обнаружение data races
- **Performance Regression** — Бенчмарки vs baseline
- **Integration** — Mock server, connection pool, OAuth2, Vault
- **E2E Smoke** — Demo services + CLI, SLA валидация
- **Lint** — clang-tidy, black, flake8, mypy, YAML валидация

## Документация

- [Contributing](CONTRIBUTING.md) — Guidelines для разработки
- [Architecture](docs/) — ADR и дизайн-документы
- [Scenarios](scenarios/) — Примеры YAML конфигураций
- [Grafana Dashboards](deploy/grafana/) — Готовые дашборды для мониторинга

## Лицензия

Apache 2.0 — см. [LICENSE](LICENSE)

---

## Автор

**ssrjkk**  
Telegram: [@ssrjkk](https://t.me/ssrjkk)  
Email: ray013lefe@gmail.com
