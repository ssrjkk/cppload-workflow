# volley Platform Architecture

This document describes the target architecture for evolving `volley` from a powerful execution engine into a real platform.

## Architectural principles

1. Keep the C++ core focused on high-rate execution.
2. Put orchestration and product logic in a separate service layer.
3. Treat load tests as first-class managed workflows with metadata and history.
4. Make result correctness and reproducibility stronger than feature count.
5. Design for multi-team, multi-environment, and multi-worker execution.

## Target system layout

```text
┌──────────────────────────────────────────────┐
│            Web / API Clients                  │
│  Browser, CLI, GitHub Actions, CI runners    │
└───────────────────────┬──────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────┐
│      Control Plane / API / Orchestrator       │
│  Go or Rust service                           │
│  - users / teams / orgs                      │
│  - projects / environments                   │
│  - scenario definitions                      │
│  - run scheduling                            │
│  - worker registry                           │
│  - RBAC / auth                               │
│  - result persistence                        │
└───────────────────────┬──────────────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────┐
│           Execution Workers                   │
│  C++ runner processes / K8s jobs              │
│  - scenario execution                         │
│  - HTTP/TCP/WS traffic generation             │
│  - rate limiting and concurrency              │
│  - metrics collection                         │
│  - artifact capture                           │
└───────────────────────┬──────────────────────┘
                        │
                        ▼
┌─────────────────────���────────────────────────┐
│             Data and Observability            │
│  - Postgres: metadata + run history          │
│  - object storage: artifacts / snapshots      │
│  - time-series: performance data             │
│  - Prometheus / Grafana / OTLP               │
└──────────────────────────────────────────────┘
```

## Layer responsibilities

### 1. C++ execution engine

This remains the high-performance load-generation layer.

Responsibilities:
- parse scenarios
- manage rate limiting and load profiles
- drive async HTTP/TCP/WebSocket traffic
- collect metrics with high precision
- emit results and diagnostics
- support secure auth and TLS
- expose a stable runner interface to the orchestrator

It should not own:
- users
- projects
- dashboards
- RBAC
- billing
- result history
- workflow state

### 2. Control plane / API service

This is the product layer. It coordinates execution and stores metadata.

Responsibilities:
- register users and teams
- manage projects and environments
- create and version scenarios
- schedule runs
- assign workers
- collect results from workers
- persist run history
- compare results against baselines
- apply RBAC and secret policies

Preferred implementation: Go or Rust for operational ergonomics, concurrency, and API maturity.

### 3. Worker pool

Workers are execution units. They should be isolated, health-checked, and stateless beyond local execution.

Worker responsibilities:
- accept runnable scenario payloads
- fetch secrets or config from the control plane if needed
- execute a load test
- stream metrics/results back
- write artifacts and diagnostics
- report health and status

### 4. Metadata and analytics storage

The platform should persist:
- project and environment metadata
- run records and status transitions
- scenario revisions
- baseline records
- timed latency/throughput/error summaries
- artifacts for debug and comparison

Recommended stack:
- PostgreSQL for structured metadata and relationships
- object storage for large artifacts and dumps
- time series storage for trend analytics
- Prometheus/Grafana for operational observability

### 5. Dashboard and developer workflow

The dashboard should provide:
- recent runs
- comparison across commits or environments
- p50/p95/p99 charts
- SLA fail reasons
- run drill-down
- historical trends
- CI PR integration

This is the layer that turns a technical runner into a product that teams trust.

## Required product entities

The platform must model these entities explicitly:

- organization
- team
- project
- environment
- scenario
- scenario version
- run
- worker
- baseline
- result snapshot
- comparison report
- alert rule
- SLO policy
- artifact
- audit log

Without these, the platform remains a CLI-only utility and not a real system.

## Execution flow

### Local-first workflow

```text
Developer writes scenario
  -> scenario validates locally
  -> scenario stored / versioned
  -> run created via API
  -> worker executes test
  -> metrics streamed to control plane
  -> artifact and summary stored
  -> dashboard compares to baseline
  -> PR gate decision is made
```

### CI workflow

```text
PR opens
  -> CI invokes volley
  -> run executed in isolated worker
  -> metrics compared to baseline
  -> if threshold exceeded, CI fails
  -> report is posted back to PR
```

### Production validation workflow

```text
Release candidate created
  -> production-like environment selected
  -> run scheduled on worker pool
  -> trend compared to target SLO
  -> alert if degraded
  -> release decision supported by evidence
```

## Delivery roadmap by architecture layer

### Sprint/block 1: engine maturation
- stable CLI contract
- scenario schema enforcement
- result contract and versioning
- benchmark gate in CI

### Sprint/block 2: API and metadata
- project and environment model
- run persistence
- baseline tracking
- compare pipeline

### Sprint/block 3: product UX
- dashboard v1
- trend visuals and drill-down
- CI result reporting
- access control

### Sprint/block 4: distributed execution
- worker pool manager
- queue and orchestration
- environment isolation
- health monitoring

### Sprint/block 5: governance and enterprise
- enterprise auth
- audit logging
- quotas and retention
- policy enforcement

## Architectural constraints

- The control plane must not become a second execution engine.
- The C++ core must not become a data store or API service.
- Scenario execution must be reproducible and verifiable.
- Result output must be versioned and comparable over time.
- Every environment must be explicit and identifiable.

## Final principle

The right architecture is not “one monolith that does everything.”

The right architecture is:

- a reliable mover of traffic
- a managed orchestration service
- a compare-first analytics layer
- a dashboard-driven workflow

That is how a high-performance load generator becomes a real product.

## Related documents

- [product-roadmap.md](product-roadmap.md)
- [architecture.md](architecture.md)
- [README.md](../README.md)

---

This architecture is the bridge between the current engineering prototype and a mature performance validation platform.
