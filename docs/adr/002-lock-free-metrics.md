<!-- @author ssrjkk | cppload -->
# ADR-002: Lock-free Metrics Collector

**Date:** 2026-05-08
**Status:** Accepted

## Context

The metrics collector is called on every HTTP request (50k+ RPS target per worker). Using a mutex to protect counter updates would introduce contention on the hot path. Each request requires updating: total count, success/failure count, bytes sent/received, cumulative latency, min/max latency.

## Decision

Use **lock-free atomic operations** (`std::atomic`) instead of mutex-based counters.

Rationale:
- **No contention:** At 50k RPS, even a 100ns mutex acquire adds 5ms per second of overhead. Atomics have zero contention under low-write scenarios (`fetch_add` on cache-aligned counters).
- **Cache line isolation:** Each atomic counter is on a separate cache line (via padding or separate cache lines in the struct layout), preventing false sharing.
- **Compare-exchange for min/max:** Min/max latency updates use `compare_exchange_weak` loops, which are lock-free on x86_64 and ARM64.
- **Read-only snapshot:** `snapshot()` loads all counters with `memory_order_relaxed` — consistent ordering is not required for monitoring purposes (a partially-consistent read is acceptable for observability).
- **Simpler code:** No RAII lock guards, no deadlock risk, no contention analysis.

**Measured difference:** In microbenchmarks, the lock-free approach shows <10ns median overhead per `record_request` call vs ~150ns with a shared mutex at 4 threads.

## Consequences

- Counter reads from other threads may observe slightly stale values (acceptable for monitoring).
- Min/max latency uses `compare_exchange_weak` which may retry under contention (rare at 50k RPS, ~1 retry per 10^5 updates on x86_64).
- Cannot protect complex data structures (only scalars). We accept this because all metrics are scalar counters.