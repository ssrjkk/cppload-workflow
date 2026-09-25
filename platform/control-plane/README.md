# volley control-plane scaffold

This directory contains the first product implementation layer of a real product platform for `volley`.

## What is implemented here

This service models the product layer of an engineering platform rather than just a C++ execution binary.

It currently includes:

- project records
- environment records
- scenario definitions
- run records
- status updates
- result summaries
- baseline comparison reports
- Postgres-backed data persistence via `database/sql`
- API auth using `X-API-Key`
- worker registration and queueing
- dashboard summary endpoint

The implementation is intentionally simple first, but it creates a real platform foundation without forcing the C++ engine to carry product responsibilities.

## Runtime

```bash
cd platform/control-plane
export VOLLEY_DB_DSN="postgres://postgres:postgres@localhost:5432/volley?sslmode=disable"
export VOLLEY_API_KEY="dev-local-key"
docker compose up -d
go run .
```

Then:

```bash
curl -H "X-API-Key: dev-local-key" http://localhost:8080/health
curl -H "X-API-Key: dev-local-key" http://localhost:8080/dashboard
curl -H "X-API-Key: dev-local-key" http://localhost:8080/projects
```

### Local Postgres via Docker Compose

```bash
docker compose up -d
```

## API surface

### Health

- `GET /health`

### Dashboard

- `GET /dashboard`

### Projects

- `GET /projects`
- `POST /projects`

### Environments

- `POST /environments`

### Scenarios

- `POST /scenarios`

### Workers

- `GET /workers`
- `POST /workers`
- `POST /workers/heartbeat`

### Runs

- `GET /runs`
- `POST /runs`
- `POST /runs/queue`
- `GET /runs/{id}`
- `POST /runs/status`
- `GET /runs/compare?baseline_id=...&current_id=...`

## Example payloads

### Register a worker

```json
{
  "id": "worker-01",
  "name": "staging-us-east"
}
```

### Heartbeat a worker

```json
{
  "id": "worker-01",
  "name": "staging-us-east"
}
```

### Queue a run

```json
{
  "project_id": "proj_123",
  "environment_id": "env_456",
  "scenario_id": "scn_789"
}
```

### Update status with result

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

## Next implementation layer

The next step is to layer in:

- project-level RBAC
- persistent worker heartbeat and lease handling
- run scheduler and lifecycle orchestration
- dashboard API and trend rendering
- CI regression gate integration

This is the first real move away from a C++ tool and toward a real platform.

## Related documents

- [../../docs/product-roadmap.md](../../docs/product-roadmap.md)
- [../../docs/platform-architecture.md](../../docs/platform-architecture.md)
