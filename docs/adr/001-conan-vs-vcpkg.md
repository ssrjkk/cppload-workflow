# ADR-001: Conan 2.x as Package Manager

**Date:** 2026-05-08
**Status:** Accepted

## Context

cppload-pro requires several C++ dependencies: Boost, OpenSSL, gRPC, protobuf, prometheus-cpp, nlohmann-json, civetweb. We need a package manager that can handle transitive dependency resolution, lock files for reproducible builds, and cross-platform support.

Alternatives considered: vcpkg, Conan 1.x, Conan 2.x, system package manager (apt).

## Decision

Use **Conan 2.x** as the primary package manager.

Rationale:
- **Pythonic syntax:** conanfile.py is written in Python, allowing imperative logic (conditional dependencies, version ranges, patching) that is not possible with vcpkg's manifest mode.
- **Central registry:** ConanCenter provides verified recipes for all our dependencies with consistent quality.
- **Lock files:** `conan lock create` generates a conan.lock that pins exact versions of all transitive dependencies, ensuring deterministic builds across environments.
- **Profile system:** Conan profiles properly handle compiler version, ABI (libc++ vs libstdc++), and build type across platforms.
- **CMake integration:** CMakeDeps generator produces modern CMake config files, consumed via `CMAKE_TOOLCHAIN_FILE`.
- **Cross-platform:** Works on Linux, macOS, and Windows with the same recipe.
- **CI-friendly:** Conan packages are cached in `~/.conan2` and can be shared across CI runs via hash-keyed cache actions.

vcpkg was rejected because:
- Manifest mode (vcpkg.json) lacks imperative logic.
- Lock file support is less mature (vcpkg's "versioning" is still experimental).
- Integration with cmake presets requires more boilerplate.

## Consequences

- Developers must install Conan 2.x (`pip install conan`).
- First build downloads and compiles all dependencies (cached afterwards).
- Windows builds are supported identically via the same conanfile.py.
- The conan.lock file must be regenerated when dependencies change.
