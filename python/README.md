# cppload Python SDK

Pure-Python load-testing SDK that mirrors the `cppload-pro` C++20 core. It is
dependency-light (only `PyYAML`) and can be used where the native CLI cannot be
built, or as a reference implementation for the C++ behavior.

## Install

```bash
pip install -e .          # package + SDK
pip install -e ".[dev]"   # + pytest, mypy, black, flake8
```

Requires Python 3.10+.

## Quick start

```python
from cppload import LoadTest

test = LoadTest("scenarios/smoke/load-test.yaml")
test.run()                # prints result summary
print(test.metrics.snapshot())
sla_ok = test.validate_sla()
```

## Components

| Class             | Purpose                                            |
| ----------------- | -------------------------------------------------- |
| `MetricsCollector`| Thread-safe counters, percentiles, error rate      |
| `TokenBucket`     | Thread-safe rate limiter (mirrors C++ engine)      |
| `HttpClient`      | Synchronous request wrapper                        |
| `ConnectionPool`  | Thread-safe pooled client reuse                    |
| `AuthProvider`    | API key / Bearer / OAuth2 client-credentials       |
| `VaultClient`     | HashiCorp Vault KV v2 lookups                      |
| `Tracer`          | Minimal OpenTelemetry-style span tracer            |
| `ScenarioEngine`  | YAML loader + CLI execution via `cppload-cli`      |
| `LoadTest`        | High-level orchestration                           |

## Running the CLI through the SDK

```python
from cppload import ScenarioEngine

engine = ScenarioEngine("scenarios/smoke/load-test.yaml")
assert engine.load_config()
engine.run()   # spawns cppload-cli --config ...
```

The CLI is discovered at `./build/tools/cppload-cli[.exe]`, `build-release`,
`build-shared`, or in `PATH`.

## Development

```bash
pytest                       # 30 tests
mypy --strict cppload
black --check cppload tests
flake8 cppload tests
```

## Version

Releases track the C++ core version. Current: **1.1.0** (also in
`cppload.__version__`).