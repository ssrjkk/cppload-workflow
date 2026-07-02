# Changelog

## [1.0.0] - 2026-07-02

### Added
- C++20 core with Boost.Beast/ASIO async HTTP client
- TokenBucket rate limiter with thread-safe implementation
- Connection pool for TCP/TLS connection reuse
- YAML scenario engine with env variable substitution
- OAuth2 client credentials flow with auto-refresh
- HashiCorp Vault integration (KV v2, AppRole, database creds)
- mTLS and TLS context management
- OpenTelemetry OTLP/HTTP+JSON tracing with sampling
- Prometheus /metrics endpoint with counters, histograms, gauges
- CLI tool with full set of options (--config, --rps, --duration, --auth-type, --vault-addr, --otlp-endpoint)
- HTTP worker for standalone load generation
- gRPC worker for distributed load testing (alpha)
- Python SDK with pybind11 bindings (alpha)
- Helm charts for Kubernetes deployment
- Multi-stage Docker build (<50MB runtime image)
- Grafana dashboard (overview with RPS, latency, error rate, connections)
- Docker Compose demo environment with Kong, Postgres, Redis, Kafka, Prometheus, Grafana, Jaeger
- CI pipeline with Build & Test, Coverage, AddressSanitizer, Lint
- Integration tests with mock HTTP server
- Performance benchmarks with Google Benchmark
- Architecture Decision Records (ADR) in docs/adr/

### Security
- Path traversal sanitization in Vault client
- CR/LF injection protection in HTTP client
- URL encoding for HTTP paths
- HTTP method validation
- TLS Context for all outbound connections
- Vault exception safety

### Quality
- 60+ unit tests across 8 test suites
- clang-tidy static analysis
- pre-commit hooks (trailing-whitespace, end-of-file-fixer, check-yaml, black, gitleaks, clang-format)
- Conventional Commits enforced
