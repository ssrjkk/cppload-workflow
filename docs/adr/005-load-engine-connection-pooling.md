<!-- @author ssrjkk | volley -->
# ADR-005: Load-Engine Connection Pooling and Peak-Concurrency Capping

**Date:** 2026-09-14
**Status:** Accepted

## Context

Audit item #7/#10: each load-test worker created a fresh `Http11Client`
(and therefore a fresh TCP/TLS connection) at the start of every stage and
destroyed it at stage end. With 100 workers this meant up to 100 sockets
churned per stage, and nothing was reused across the rampup/steady/spike
stages of a scenario.

A natural answer — one shared `ConnectionPool` with per-request
acquire/release — is **unsafe** here: `Http11Client` is not thread-safe for
concurrent use (its cached keep-alive socket is plain data, not
synchronized). Two workers sharing one client would race on `cached_`.

A second trap: `ConnectionPool::acquire()` returns `nullptr` once
`PoolConfig::max_connections` is reached. With workers that hold a client
for their whole lifetime, a cap below the stage concurrency would make the
pool permanently exhausted: every still-waiting worker polls a pool whose
connections are all held for the entire stage → livelock.

## Decision

- Enable pooling **only** for the `http1.1` protocol (the pool is typed to
  `Http11Client`; `tcp_raw`/`ws` keep `ProtocolFactory::create`).
- Each worker **acquires one client at start and holds it for the entire
  stage**; released back on exit. This preserves the single-owner safety
  invariant while delivering real reuse across stages and a hard cap on the
  aggregate socket count.
- `max_connections` is sized to the **peak** stage concurrency
  (`max(stage.concurrent_users)` across all stages, bounded below), so the
  bounded acquire-retry in the worker never trips in practice; the retry
  exists only to guarantee termination, never to backpressure.
- The pool inherits the engine's `TlsConfig` (`PoolConfig::tls_config`), so
  `target.tls.verify` keeps applying to pooled clients — otherwise pooled
  clients would silently fall back to the default verify policy.
- On worker exit, a client with a request still in flight (e.g. mid-`stop()`)
  is **dropped, not pooled**: its pending async completion would otherwise
  run against a future borrower.
- Worker failures are counted (`worker_failures_`); a failed worker aborts
  the stage immediately and skips the remaining stages (`run_failed()`),
  instead of letting the run "complete" with zero requests and a green SLA.

## Alternatives considered

- **Per-request acquire/release** — rejected: race on `Http11Client` state.
- **No cap enforcement (pool max ≫ concurrency)** — rejected: defeats the
  audit's purpose of bounding sockets.
- **Backpressure (blocking acquire)** — rejected: with hold-per-stage there
  is nothing to wait for; blocking would deadlock.

## Consequences

- Keep-alive connections survive stage boundaries; rampup-to-spike transitions
  stop tearing down sockets.
- Socket count is bounded by peak concurrency across the whole run.
- `target.tls.verify` semantics are preserved for pooled clients.
- A worker that fails (unexpected exception, protocol error, pool
  exhaustion) surfaces as an aborted run with a non-zero exit instead of a
  silently "successful" zero-request test.
- Holding one client per worker caps reuse to stage boundaries — acceptable
  for a load generator that mostly uses short-lived HTTP/1.1 flows.