# Aether Objects Guide

## Library

`aether-objects` is a C++20 static library for persistent object graphs. It
provides object identities, domains, registration, serialization and persistence
interfaces, reference-counted pointers, and RAM or standard-filesystem storage.
Public headers are installed from `src/aether-objects`, and the library target is
`aether-objects` (alias `aether::objects`).

## Coding Style and Contributor Agreement

- Follow the repository `.clang-format` configuration (Google C++ style) and
  format repository-owned C++ sources and headers before submitting changes.
- Preserve the C++20 language level, dependency versions, public include root,
  target identity, and installation layout unless a change explicitly requires
  them.
- Public header guards use `AETHER_OBJECTS_<PATH>_H_`, derived from the path
  beneath `src/aether-objects`; closing comments must match the guard.
- Keep includes minimal. Preserve intentional exported or transitive includes
  with their IWYU annotations.
- Raw pointers express nullable or non-owning access, never ownership. Use
  `Ptr<T>`, `PtrView<T>`, and `ObjPtr<T>` according to their established
  lifetime contracts.
- Keep serialization formats, object IDs, ownership and reference tracking,
  persistence behavior, and threading behavior compatible.
- Use `LOG_` macros for local debug logging.
- Manage dependencies through CPM; do not vendor, edit, or copy dependency
  sources without explicit approval.

## Modules

### Pointer (`ptr`)

`Ptr<T>` supplies reference-counted ownership with reference-tree tracking to
reclaim cycles. `PtrView<T>` is a nullable non-owning view and must be locked or
loaded before retaining or dereferencing its object. Avoid unnecessary `Ptr`
copies because releasing one can traverse the reference graph.

### Objects and Domains (`obj`)

Objects provide durable IDs, versions, registration, typed object pointers, and
domain membership. Domains coordinate object lookup, loading, saving, and
registration through the persistence interface. A valid `ObjPtr` can designate
an unloaded object; load it and retain the loaded pointer while using it.

### Domain Storage (`domain_storage`)

`IDomainStorage` defines persistence operations used by domains. The available
implementations are in-memory RAM storage and standard-filesystem storage.
Preserve their serialized representation and error behavior.

### Logging (`log.h`)

`LOG_` is the local debug logging boundary. It writes formatted messages in
debug builds unless `AE_NO_DEBUG_LOG` disables it, and compiles away in release
builds. Preserve this behavior.

## Tests and Dependencies

- Tests use Unity and are organized under `tests/` by subsystem. Test suites
  expose a global `int test_<suite>()` entry and use the existing naming style.
- Configure builds with CMake in a separate build directory. Build and run
  configured tests with `cmake --build <build_dir> --parallel` and
  `ctest --test-dir <build_dir> --output-on-failure`.
- Use the configured build's `compile_commands.json` for changed-file
  clang-tidy analysis. Treat findings that require public API, ownership,
  persistence, threading, or platform changes as requiring review.
- Generated files, build outputs, and third-party dependency sources are not
  formatted or edited.
