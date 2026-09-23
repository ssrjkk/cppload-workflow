<!-- @author ssrjkk | volley -->
# volley Documentation

## Architecture Overview

Load-testing tool for HTTP services. C++20 core (Boost.Beast/ASIO).

### Components

- **Core Engine** (`core/`) — Async HTTP/gRPC with Boost.Beast/ASIO
- **Metrics** (`core/metrics/`) — Lock-free metrics collection
- **OTEL** (`core/otel/`) — OpenTelemetry distributed tracing
- **Security** (`core/security/`) — Auth providers (OAuth2, mTLS)
- **Workers** (`workers/`) — Executable load generators
- **Python SDK** (`python/`) — Orchestration and reporting

### Performance Targets

- 10k+ RPS per node (HTTP/1.1, small payloads, localhost)
- <1ms overhead per request

## Building

```bash
# Configure
cmake -B build -G Ninja -DVOLLEY_BUILD_PYTHON=ON

# Build
cmake --build build

# Test
cd build && ctest
```

## Configuration

See `scenarios/ecommerce/load-test.yaml` for example.