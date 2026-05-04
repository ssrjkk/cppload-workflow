# cppload-pro

[![CI](https://github.com/your-org/cppload-pro/actions/workflows/ci.yml/badge.svg)](https://github.com/your-org/cppload-pro/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C++-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)
[![Python](https://img.shields.io/badge/python-3.9+-green.svg)](https://www.python.org/)

**Enterprise Load Testing Platform** — высокопроизводительная система нагрузочного тестирования с ядром на C++20 и оркестрацией на Python.

## 🎯 Возможности

| Фича | Статус | Описание |
|------|--------|----------|
| **Async HTTP/1.1 Client** | ✅ MVP | Boost.Beast + ASIO, lock-free метрики |
| **Prometheus Exporter** | ✅ DONE | HTTP /metrics endpoint, histograms, gauges |
| **OpenTelemetry Tracing** | ✅ MVP | Distributed tracing (заглушка, готов API) |
| **Python SDK** | ✅ MVP | pybind11 bindings, LoadTest orchestration |
| **Auth (OAuth2/mTLS)** | 🚧 In Progress | AuthProvider интерфейс готов |
| **Kubernetes Deployment** | 📋 Planned | Helm charts |
| **HashiCorp Vault** | 📋 Planned | Secrets management |

## 🏗️ Архитектура

```
┌─────────────────────────────────────────────────┐
│  CONTROL PLANE (Python)                        │
│  • CLI Tool (Boost.ProgramOptions)             │
│  • Scenario Engine (YAML configs)              │
│  • Reporters (Allure, JUnit, Prometheus)       │
└────────────────┬────────────────────────────────┘
                 │ gRPC / Protocol Buffers
                 ▼
┌─────────────────────────────────────────────────┐
│  DATA PLANE (C++20 Workers)                    │
│  ┌───────────────────────────────────────────┐ │
│  │  Core Engine                              │ │
│  │  • Boost.Beast HTTP Client               │ │
│  │  • Lock-free Metrics Collector            │ │
│  │  • OpenTelemetry Exporter                 │ │
│  └───────────────────────────────────────────┘ │
└────────────────┬────────────────────────────────┘
                 │
         ┌───────┴───────┐
         ▼               ▼
    [Target: HTTP]  [Target: gRPC]
```

## 🚀 Быстрый старт

### Сборка C++ ядра

```bash
# Установка зависимостей через Conan
conan install . --output-folder=build --build=missing

# Конфигурация (требуется Boost 1.83+, OpenSSL 3.0+, prometheus-cpp)
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPLOAD_BUILD_PYTHON=ON \
  -DCPLOAD_BUILD_TESTS=ON

# Сборка
cmake --build build

# Запуск тестов (включая Prometheus exporter)
cd build && ctest --output-on-failure
```

### Запуск с Prometheus метриками

```cpp
#include "cppload/metrics/prometheus_exporter.hpp"
#include "cppload/metrics/collector.hpp"

cppload::metrics::MetricsCollector collector;
cppload::metrics::PrometheusExporter exporter("0.0.0.0:9090");

exporter.start();  // Запуск HTTP сервера на порту 9090

// В цикле теста
collector.record_request(200, std::chrono::microseconds(100), 100, 500);
exporter.update_metrics(collector);  // Обновление метрик

// Prometheus доступен на http://localhost:9090/metrics
```

### Установка Python SDK

```bash
pip install -e .
```

### Запуск демо-стенда

```bash
# Поднять инфраструктуру (Kong, микросервисы, Kafka, мониторинг)
docker-compose -f deploy/demo-env/docker-compose.yml up -d

# Доступы:
# - Gateway: http://localhost:8080
# - Grafana: http://localhost:3000
# - Prometheus: http://localhost:9090
# - Jaeger UI: http://localhost:16686
```

### Выполнение нагрузочного теста

```bash
# CLI
./build/tools/cppload-cli --target http://localhost:8080 --rps 1000 --duration 60

# Python SDK
python -c "from cppload import LoadTest; t = LoadTest('scenarios/ecommerce/load-test.yaml'); t.run()"
```

## 📊 Пример конфигурации (YAML)

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
          assertions:
            - status_code == 201
            - latency_p95 < 200ms

sla:
  error_rate: "< 0.1%"
  p99_latency: "< 500ms"
```

## 📂 Структура проекта

```
cppload-pro/
├── core/                    # C++20 ядро
│   ├── net/                 # HTTP/gRPC клиенты
│   ├── metrics/             # Сбор метрик (lock-free)
│   ├── security/            # Auth (OAuth2, mTLS)
│   └── otel/                # OpenTelemetry
├── workers/                 # Исполняемые воркеры
├── python/                  # Python SDK + pybind11
├── tools/                   # CLI утилиты
├── tests/                   # GTest тесты
├── deploy/                  # Инфраструктура
│   ├── demo-env/            # Docker Compose стенд
│   └── kubernetes/          # Helm charts
├── scenarios/               # Библиотека сценариев
└── docs/                    # ADR, спецификации
```

## 🔧 Технологический стек

**C++ Core:**
- C++20 (coroutines, concepts)
- Boost.Beast / Boost.ASIO (async I/O)
- OpenSSL (TLS/mTLS)
- Prometheus-cpp (метрики)
- OpenTelemetry-cpp (трейсинг)

**Python:**
- pybind11 (C++ bindings)
- PyYAML (конфиги)
- pytest (тестирование)

**Infrastructure:**
- Docker / Docker Compose
- Kubernetes / Helm
- Kafka (асинхронное взаимодействие)
- Prometheus + Grafana (мониторинг)
- Jaeger (трейсинг)

## 📈 Roadmap

- [x] **Core MVP** — Async HTTP client, базовые метрики
- [x] **Python bindings** — pybind11 интеграция
- [ ] **Prometheus exporter** — /metrics endpoint
- [ ] **Auth providers** — OAuth2, API Key, mTLS
- [ ] **Distributed mode** — gRPC контроллер + воркеры
- [ ] **Kubernetes** — Helm charts для деплоя
- [ ] **Vault integration** — безопасное управление секретами

## 🤝 Contributing

См. [CONTRIBUTING.md](CONTRIBUTING.md). ADR (Architecture Decision Records) находятся в `docs/`.

## 📄 License

Apache 2.0 — см. [LICENSE](LICENSE).

---

> **Для резюме:** Проектирование и разработка распределенной системы нагрузочного тестирования с ядром на C++20 (50k+ RPS на ноду), интеграцией OpenTelemetry и деплоем в Kubernetes.
