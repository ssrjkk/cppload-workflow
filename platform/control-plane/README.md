# volley control-plane scaffold

This directory contains the first product implementation layer for `volley`: a minimal control plane that models the productized business logic needed before full distributed worker orchestration.

## Purpose

The goal of this scaffold is to capture the product model explicitly:

- projects
- environments
- scenarios
- runs
- result summaries
- baseline comparison

This creates the foundation for a real platform without forcing the C++ engine to carry product responsibilities.

## Endpoints

### Health

- `GET /health`

### Projects

- `GET /projects`
- `POST /projects`

Example payload:

```json
{
  "name": "checkout-service",
  "description": "API performance validation for checkout"
}
```

### Environments

- `POST /environments`

Example payload:

```json
{
  "project_id": "proj_123",
  "name": "staging",
  "type": "staging"
}
```

### Scenarios

- `POST /scenarios`

Example payload:

```json
{
  "project_id": "proj_123",
  "name": "checkout-smoke",
  "version": "1.0",
  "yaml": "version: \"1.0\"\n"
}
```

### Runs

- `GET /runs`
- `POST /runs`
- `GET /runs/{id}`
- `POST /runs/status`
- `GET /runs/compare?baseline_id={id}&current_id={id}`

Example run-status update:

```json
{
  "run_id": "run_123",
  "status": "succeeded",
  "result": {
    "total_requests": 120000,
    "successful_requests": 119920,
    "failed_requests": 80,
    "error_rate_pct": 0.07,
    "throughput_rps": 4200,
    "p95_latency_ms": 180,
    "p99_latency_ms": 260
  }
}
```

## Execution

Run the scaffold locally:

```bash
cd platform/control-plane
go run .
```

Then test it:

```bash
curl http://localhost:8080/health
curl http://localhost:8080/projects
```

## Product significance

This is the first concrete step away from “C++ tool + scripts” and toward "platform with state, comparison, and run history".

The next implementation layers will be:

- authenticated API
- PostgreSQL persistence
- worker orchestration
- dashboard and trend rendering
- CI regression gates

## Related docs

- [../../docs/product-roadmap.md](../../docs/product-roadmap.md)
- [../../docs/platform-architecture.md](../../docs/platform-architecture.md)
