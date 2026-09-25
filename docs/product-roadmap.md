# volley Product Roadmap

This document defines the productization path for `volley` from a high-performance execution engine into a production-grade performance validation platform.

## Mission

Turn `volley` from a technical load-testing binary into a platform that helps engineering teams answer a single question reliably:

> Did this change make the system faster, slower, or riskier under realistic load?

## Product direction

The product must evolve in layers, not by adding more features to the C++ core alone.

### Target product definition

`volley` should become a performance validation platform for platform, API, backend, and SRE teams. It should provide:

- scenario-driven load tests
- baseline comparison across revisions and environments
- CI integration as a release gate
- shared team dashboards and run history
- multi-environment execution
- secure secret and auth handling
- long-lived comparisons and alerting

## Product maturity model

### Phase 1 — Core stability and contract discipline

Goal: make the execution engine reliable, versioned, and reproducible.

Scope:
- freeze scenario schema and validate it strictly
- formalize run metadata and result contracts
- define error taxonomy and exit codes
- enforce benchmark baselines for hot paths
- add sanitizer and race-validation gates in CI
- define compatibility policy for YAML and output formats

Exit criteria:
- every scenario validates against a versioned schema
- every result is captured in a stable JSON contract
- every PR changes benchmark behavior only with explicit review
- all critical paths pass sanitizer and long-run stability tests

### Phase 2 — Team platform

Goal: transform local runner into managed team workflow.

Scope:
- run history and persistence
- comparison against baseline
- project and environment model
- API layer for scheduling and results
- RBAC and secret handling
- artifact retention and trace linkage
- first dashboard for runs and trends

Exit criteria:
- a team can create a project, run a test, review results, and compare to baseline
- runs are persisted and queryable
- secrets are not exposed in logs or artifacts
- stakeholders can inspect performance regressions without using the CLI

### Phase 3 — Platform and observability

Goal: turn single-run capability into an operational platform.

Scope:
- distributed worker pool
- K8s-based execution for multi-environment tests
- baseline policies and performance gates
- alerting on SLA breaches
- dashboards for latency, throughput, and error-rate trends
- CI pull request comments with performance deltas
- export of results for analytics and external reporting

Exit criteria:
- engineering teams use `volley` as a gated workflow in CI/CD
- failures are visible before release
- historical trends can be compared over time
- multi-environment validation is repeatable and automated

### Phase 4 — Enterprise hardening

Goal: support serious internal or external adoption.

Scope:
- multi-tenancy
- quotas and limits
- SSO and enterprise auth
- audit logs
- retention policies
- backup and restore
- run policy enforcement
- per-team isolation and governance

Exit criteria:
- enterprise teams can run isolated workloads safely
- operators can audit access and changes
- data retention and policy controls are enforceable

### Phase 5 — Managed product

Goal: ship a SaaS or managed self-hosted product.

Scope:
- elastic cloud workers
- usage metering and billing
- data residency controls
- managed workers for multiple regions
- enterprise support workflows
- onboarding and tenant-level automation

Exit criteria:
- product is operated as a service, not as a repo-driven tool
- onboarding is self-serve
- operational burden is small for users

## Non-negotiable rules

The project must follow these rules as it matures:

1. C++ remains the execution engine, not the product control plane.
2. Every feature in the control plane needs a data model and persistence contract.
3. Every performance-related change must include benchmark evidence.
4. No scenario should be accepted without strict schema validation.
5. No secret or token may be emitted in logs, artifacts, or traces.
6. No user-facing result should be valid without versioning metadata.
7. Platform health must be observable using its own metrics.

## Prioritized execution order

The first practical sequence is:

1. scenario schema v1 and validation
2. run metadata + result persistence
3. baseline comparison
4. CI regression gates
5. dashboard v1
6. worker orchestration
7. org/project/environment model
8. RBAC and secrets
9. alerts and SLO policies
10. multi-environment scaling

This order creates a real product loop faster than adding more engine features alone.

## Concrete roadmap deliverables

### Q1: stabilize the engine
- strict scenario validation
- benchmark gating policy
- result schema versioning
- CI sanitizer gate
- memory safety pass

### Q2: add platform metadata and compare workflows
- projects, environments, runs
- run history UI/API
- baseline repository
- compare reports

### Q3: add workflow integration and dashboards
- PR and CI gate integration
- dashboard v1
- historical charting
- run drill-down

### Q4: scale to fleet and governance
- worker pool
- RBAC
- secrets and audit
- alerts and SLOs

## Final product thesis

`volley` should become the system that tells engineering teams, with evidence, whether a service is healthier, slower, or riskier under load — before it reaches production.

That is the real product. The rest is implementation detail.

## Related documents

- [platform-architecture.md](platform-architecture.md)
- [architecture.md](architecture.md)
- [README.md](../README.md)

---

This roadmap is deliberately oriented toward product maturity and operational reliability rather than raw technical novelty.
