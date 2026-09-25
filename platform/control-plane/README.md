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
- worker registration, heartbeats, and queueing
- project membership mapping
- CI-style run policy evaluation
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
- `POST /projects/{project_id}/members`

### Environments

- `POST /environments`

### Scenarios

- `POST /scenarios`

### Workers

- `GET /workers`
- `POST /workers`
- `POST /workers/heartbeat`

### Policies

- `GET /policies?project_id=...`
- `POST /policies`

### Runs

- `GET /runs`
- `POST /runs`
- `POST /runs/queue`
- `POST /runs/evaluate`
- `GET /runs/{id}`
- `POST /runs/status`
- `GET /runs/compare?baseline_id=...&current_id=...`

## Example payloads

### Add project member

```json
{
  "user_id": "alice",
  "role": "owner"
}
```

### Create a gate policy

```json
{
  "project_id": "proj_123",
  "name": "release-gate",
  "max_error_rate_pct": 0.5,
  "max_p99_latency_ms": 300
}
```

### Evaluate a run against policy

```json
{
  "project_id": "proj_123",
  "run_id": "run_901",
  "result": {
    "total_requests": 125000,
    "successful_requests": 124680,
    "failed_requests": 320,
    "error_rate_pct": 0.26,
    "throughput_rps": 4500,
    "p95_latency_ms": 210,
    "p99_latency_ms": 280
  }
}
```

### Register a worker

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

## Next implementation layer

The next step is to layer in:

- persistent project memberships in DB
- durable policy storage in DB
- worker lease assignment with run scheduling
- dashboard trend rendering and historical thresholds
- CI regression gate integration based on `runs/evaluate`

This is the first real move away from a C++ tool and toward a real platform.

## Related documents

- [../../docs/product-roadmap.md](../../docs/product-roadmap.md)
- [../../docs/platform-architecture.md](../../docs/platform-architecture.md)
