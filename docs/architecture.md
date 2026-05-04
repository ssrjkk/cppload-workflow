# Architecture Decision Records

## ADR-001: C++20 Core with Boost.Beast

**Status:** Accepted

**Context:** Need high-performance HTTP client for load generation.

**Decision:** Use C++20 with Boost.Beast for async I/O.

**Consequences:**
- ✅ 50k+ RPS per node achievable
- ✅ Zero-copy where possible
- ❌ Steeper learning curve than Go/Java

## ADR-002: Python Control Plane

**Status:** Accepted

**Context:** Need flexible orchestration and reporting.

**Decision:** Use Python with pybind11 for C++ bindings.

**Consequences:**
- ✅ Rapid scenario development
- ✅ Rich ecosystem (Allure, Prometheus)
- ❌ GIL limitations (mitigated by C++ workers)

## ADR-003: OpenTelemetry for Observability

**Status:** Accepted

**Context:** Need distributed tracing across loader and target.

**Decision:** Integrate OpenTelemetry C++ SDK.

**Consequences:**
- ✅ Vendor-neutral standard
- ✅ Compatible with Jaeger/Zipkin
- ❌ Additional dependency
