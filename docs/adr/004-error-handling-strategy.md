<!-- @author ssrjkk | cppload -->
# ADR-004: Error Handling Strategy

**Date:** 2026-07-02
**Status:** Accepted

## Context

cppload-pro operates in a distributed, network-intensive environment where failures are expected: DNS resolution failures, connection timeouts, TLS handshake errors, Vault unavailability, OAuth2 token expiry, and YAML parsing errors. We need a consistent strategy for error handling across all components.

## Decision

Use a **layered error handling strategy** with three tiers:

### Tier 1: Return-value errors (recoverable)

Network operations (HTTP requests, Vault calls, OAuth2 token refresh) return empty results or error flags rather than throwing exceptions. This allows the caller to decide whether to retry, skip, or fail.

Examples:
- `VaultClient::get_secret()` returns empty string on failure, sets `last_error_`
- `TokenBucket::try_consume()` returns `bool`
- `AuthProvider::refresh_token()` returns `bool`
- `HttpClient` calls callback with `status_code == 0` on failure

### Tier 2: Exception throwing (unrecoverable contract violations)

Constructor validation and configuration errors throw exceptions because they indicate programming errors that cannot be recovered at runtime.

Examples:
- `TokenBucket(double rate)` throws `std::invalid_argument` if `rate <= 0`
- `TlsContext` throws `std::invalid_argument` if mTLS is enabled but cert/key files are missing
- `AuthProvider` throws `std::runtime_error` if OAuth2 token fetch fails during construction

### Tier 3: Noexcept for destructors and cleanup

Destructors and cleanup code are marked `noexcept` to guarantee exception safety during stack unwinding.

Examples:
- `~ScenarioEngine() noexcept`
- `~Tracer() noexcept`

### Error propagation pattern

```
User-facing code (CLI/Python SDK)
  └─ catches exceptions, displays user-friendly error messages
       └─ ScenarioEngine / AuthProvider / VaultClient
            └─ returns error codes / empty results for network errors
                 └─ throws exceptions for contract violations
```

### Error reporting

Each component with tier-1 errors provides a `last_error()` method that returns a human-readable string describing the most recent error. This string is used by the CLI tool and Python SDK for error reporting.

## Consequences

- Network errors are non-fatal by default — the caller decides the recovery strategy.
- Programming errors (invalid args, missing files) fail fast with an exception.
- Exception safety is guaranteed in destructors.
- Each component owns its error reporting via `last_error()`.
- No single global error state — errors are scoped to the component instance.