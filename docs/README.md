# cppload-pro Documentation

## Architecture Overview

Enterprise load testing platform with C++20 core for maximum performance.

### Components

- **Core Engine** (`core/`) — Async HTTP/gRPC with Boost.Beast/ASIO
- **Metrics** (`core/metrics/`) — Lock-free metrics collection
- **OTEL** (`core/otel/`) — OpenTelemetry distributed tracing
- **Security** (`core/security/`) — Auth providers (OAuth2, mTLS)
- **Workers** (`workers/`) — Executable load generators
- **Python SDK** (`python/`) — Orchestration and reporting

### Performance Targets

- 50k+ RPS per node (HTTP/1.1)
- 30k+ RPS with HTTP/2
- <1ms overhead per request

## Building

```bash
# Configure
cmake -B build -G Ninja -DCPLOAD_BUILD_PYTHON=ON

# Build
cmake --build build

# Test
cd build && ctest
```

## Configuration

See `scenarios/ecommerce/load-test.yaml` for example.
