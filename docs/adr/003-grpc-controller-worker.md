<!-- @author ssrjkk | cppload -->
# ADR-003: gRPC for Distributed Mode

**Date:** 2026-05-08
**Status:** Accepted

## Context

cppload-pro supports a distributed mode where a Controller (Python) assigns load test tasks to Workers (C++), collects real-time metrics, and orchestrates the test lifecycle. We need a communication protocol that supports:

- Typed, versioned contracts between Controller and Worker
- Bidirectional streaming of metrics during a test run
- TLS/mTLS for secure communication
- Low latency for control messages

## Decision

Use **gRPC (Protocol Buffers)** for the Controller-Worker communication protocol.

Alternatives considered: REST (JSON over HTTP), raw TCP sockets, message queue (Kafka/NATS).

Rationale:
- **Typed contracts:** protobuf `.proto` files define the exact request/response schema. Breaking changes are detected at compile time, not at runtime as with JSON REST.
- **Bidirectional streaming:** gRPC server-side streaming allows Workers to push metrics continuously without polling. REST would require either WebSocket (more complex) or polling (higher latency).
- **Streaming RPCs:** `RegisterWorker` streams metrics upward; `AssignTask` streams tasks downward to a pool of workers.
- **Already in the stack:** cppload-pro already uses protobuf for configuration serialization, so gRPC adds no new dependency type (only the gRPC runtime on top of protobuf).
- **TLS natively:** gRPC supports TLS/mTLS out of the box via the same OpenSSL integration we already use.
- **Performance:** gRPC over HTTP/2 provides low-latency multiplexed streams. Protobuf binary encoding is ~10x faster than JSON parsing.

REST was rejected because:
- JSON serialization is ~10x slower than protobuf for the same data.
- No native streaming — requires SSE or WebSocket.
- No typed contracts — breaking changes are runtime errors.

Message queue was rejected because:
- Adds operational complexity (Kafka/ZooKeeper cluster).
- Higher latency for control messages.
- Overkill for the Controller-Worker pattern (N-to-N topology not needed).

## Consequences

- Workers must link gRPC, increasing binary size by ~3MB (static).
- Protobuf schema changes require coordinated deployment of Controller and Workers.
- gRPC health checking and load balancing require additional configuration in Kubernetes.