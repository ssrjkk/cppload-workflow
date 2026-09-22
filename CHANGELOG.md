<!-- @author ssrjkk | cppload -->
# Changelog

## [Unreleased]

## [1.1.0] - 2026-09-22

### Added
- Python SDK: 143 tests with 100% coverage (460/460 statements)
- Python SDK: 27 integration tests (real HTTP server, subprocess CLI calls)
- Python SDK: 67 unit tests without mocks (pure business logic)
- Comprehensive security hardening across C++ core
- Thread-safety improvements in engine, vault, auth, metrics, and HTTP client
- Pooled HTTP/1.1 workers for better throughput
- Benchmark regression gating in CI

### Fixed
- OpenTelemetry OTLP exporter now exports on a background thread; an
  unreachable collector can no longer stall the request hot path at ~1s per
  batch. Export is disabled by default until an OTLP endpoint is configured,
  and failed exports back off (200ms..5s) instead of spamming stderr
- Demo services (`services/orders`, `services/products`) now speak HTTP/1.1
  with connection keep-alive, eliminating connection churn and the high error
  rate seen during local e2e runs
- E2E smoke scenario no longer asserts a service-specific path (e.g. `/orders`
  against the products service); the verified endpoint is selected per service
  via the `SMOKE_PATH` environment variable
- Engine deadline handling and io_context hang issues
- Beast deadline enforcement on sync paths
- Dead keep-alive connection detection
- Metrics ring buffer eviction
- Benchmark JSON merge producing string entries
- Benchmark binary path in JSON medians step
- Unused variables causing build failures with -Werror

### Changed
- Removed marketing/enterprise wording from the README, package metadata,
  docs, CLI `--help` text, and Helm chart description
- Python SDK CLI discovery now also checks `build-shared` and picks up the
  `.exe` suffix on Windows; `PyYAML` is declared as a runtime dependency

### Added
- E2E smoke CI job: builds the CLI and runs the YAML scenario against both
  demo services, failing on SLA breach
- Performance benchmark job in CI regenerates and commits
  `benchmarks/baseline.json` on pushes to `main` (`[skip ci]`)

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