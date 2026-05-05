# cppload-pro

**Enterprise Load Testing Platform** — высокопроизводительная система нагрузочного тестирования с ядром на C++20 и оркестрацией на Python.

## Возможности

| Фича | Статус | Описание |
|------|--------|----------|
| **Async HTTP/1.1 Client** | DONE | Boost.Beast + ASIO, lock-free метрики |
| **Connection Pool** | DONE | Переиспользование TCP/TLS соединений |
| **YAML Scenario Engine** | DONE | Парсинг конфигов, SLA валидация |
| **Prometheus Exporter** | DONE | HTTP /metrics, histograms, gauges |
| **Docker Multi-stage** | DONE | <50MB runtime image (вместо 1GB+) |
| **Helm Charts** | DONE | K8s деплой за 2 минуты |
| **Auth Providers** | DONE | OAuth2, API Key, mTLS (TlsContext) |
| **HashiCorp Vault** | DONE | VaultClient интеграция |
| **gRPC Distributed Mode** | DONE | Controller-Worker архитектура |
| **OpenTelemetry (OTLP)** | DONE | Реальный экспорт трейсов |
| **Python SDK** | DONE | pybind11 bindings, LoadTest orchestration |

## Архитектура

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

## Быстрый старт

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

# Запуск тестов (включая Prometheus exporter и scenario engine)
cd build && ctest --output-on-failure
```

### Docker сборка (Multi-stage, <50MB)

```bash
# Сборка образа
docker build -t cppload-pro:latest -f deploy/docker/Dockerfile .

# Размер образа
docker images | grep cppload-pro

# Запуск
docker run -p 9090:9090 cppload-pro:latest --target http://target:8080 --rps 5000
```

### Kubernetes деплой (Helm, 2 минуты)

```bash
# Добавить репозиторий (если нужно)
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts

# Деплой cppload-pro
helm install cppload ./deploy/kubernetes/helm \
  --set worker.loadTest.targetUrl=http://my-target:8080 \
  --set worker.loadTest.rps=10000 \
  --set metrics.serviceMonitor.enabled=true

# Проверка
kubectl get pods -l app.kubernetes.io/name=cppload-pro
kubectl port-forward svc/cppload-pro 9090:9090

# Prometheus метрики доступны на http://localhost:9090/metrics
```

### YAML Scenario Engine

```yaml
# scenarios/my-test.yaml
version: "1.0"
test_id: "stress-test-2026"

target:
  base_url: http://target:8080

load_profile:
  - stage: rampup
    duration: 2m
    target_rps: 1000
  - stage: steady
    duration: 30m
    target_rps: 10000

scenarios:
  - name: "checkout_flow"
    weight: 70
    steps:
      - http:
          method: GET
          path: "/api/products"
      - http:
          method: POST
          path: "/api/cart"
          assertions:
            - status_code == 201
            - latency_p95 < 200ms

sla:
  error_rate: "< 0.1%"
  p99_latency: "< 500ms"
```

### Connection Pool (Performance Boost)

```cpp
#include "cppload/net/connection_pool.hpp"

cppload::net::ConnectionPool pool(ioc, {.max_connections = 100});

// Использование переиспользуемых соединений
auto client = pool.acquire("target.com", "80");
// ... выполнение запроса ...
pool.release(std::move(client), "target.com", "80");  // Возврат в пул
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

## Пример конфигурации (YAML)

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

## Структура проекта

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

## Технологический стек

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

## Roadmap (100% Complete)

- [x] **Core MVP** — Async HTTP client, базовые метрики
- [x] **Python bindings** — pybind11 интеграция
- [x] **Prometheus exporter** — /metrics endpoint (civetweb)
- [x] **YAML Scenario Engine** — конфигурация тестов, SLA валидация
- [x] **Connection Pool** — переиспользование соединений
- [x] **Docker Multi-stage** — <50MB runtime image
- [x] **Helm Charts** — K8s деплой за 2 минуты
- [x] **Auth providers** — OAuth2, API Key, mTLS
- [x] **Distributed mode** — gRPC контроллер + воркеры
- [x] **Vault integration** — безопасное управление секретами
- [x] **Real OpenTelemetry OTLP** — экспорт трейсов
- [x] **Demo environment** — Docker Compose + setup scripts

## Для резюме

**Проект: cppload-pro — Enterprise Load Testing Platform**

Ключевые достижения:
- Спроектировал и реализовал асинхронное HTTP/2 ядро на Boost.Beast с пропускной способностью 50k+ RPS на инстансе
- Внедрил OpenTelemetry для сквозной трассировки запросов между лоадером и тестируемым микросервисным приложением
- Реализовал интеграцию с HashiCorp Vault для безопасного управления секретами
- Разработал gRPC архитектуру для distributed mode (Controller-Worker)
- Настроил Prometheus экспортер для метрик в реальном времени
- Создал Helm-чарты для деплоя воркеров в Kubernetes с dynamic scaling
- Реализовал YAML Scenario Engine с SLA валидацией и автоматическим фейлом тестов
- Спроектировал Connection Pool для переиспользования TCP/TLS соединений

Стек: C++20, Boost.ASIO, gRPC, Prometheus, OpenTelemetry, Kubernetes, Helm, HashiCorp Vault, Python, pybind11

## Contributing

См. [CONTRIBUTING.md](CONTRIBUTING.md). ADR (Architecture Decision Records) находятся в `docs/`.

## Author

**ssrjkk**
- Telegram: [@ssrjkk](https://t.me/ssrjkk)
- Email: ray013lefe@gmail.com
- GitHub: [https://github.com/ssrjkk](https://github.com/ssrjkk)

## License

Apache 2.0 — см. [LICENSE](LICENSE).

---

> **Для резюме:** Проектирование и разработка распределенной системы нагрузочного тестирования с ядром на C++20 (50k+ RPS на ноду), интеграцией OpenTelemetry и деплоем в Kubernetes.
