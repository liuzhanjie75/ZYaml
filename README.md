# ZYaml

A modern C++23 YAML 1.2 core library — modular, comment-preserving, std::expected-based.

> **Status:** early development (M0 — repository skeleton). Not ready for use.

## Goals

- **YAML 1.2 core spec**: block + flow collections, anchors/aliases, tags, merge keys (`<<`), block scalars (`|`/`>` with chomping/indent indicators), quoted + plain scalars, document markers.
- **Comment preservation**: comments are attached to nodes at parse time and survive parse → mutate → serialize round-trips.
- **Insertion-order maps**: maps are stored as ordered `(key, value)` pairs — no silent key reordering.
- **Modern C++23**: pure `export module ZYaml;`, `std::expected` for parse errors (no exceptions by default), `std::string_view`, ranges, concepts.
- **No external runtime dependencies**.

## Building

Requires CMake 4.0+ and either:
- MSVC 19.44+ (`CMAKE_CXX_SCAN_FOR_MODULES` is enabled automatically), or
- Clang 20+ with `clang-scan-deps` on `PATH` (or `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS`).

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

MIT (see `LICENSE`).
