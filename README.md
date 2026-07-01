# ZYaml

A C++23 YAML 1.2 library — pure modules, comment-preserving, insertion-ordered, with a `Result<T>` error model (no exceptions).

## Features

- **Block maps & sequences** with arbitrary nesting
- **Flow style** `[a, b]` and `{k: v}`, including nested and multi-line flow per YAML 1.2
- **Quoted scalars** — double-quoted with full escape set (`\n \t \r \\ \" \' \0 \a \b \f \v \e`) and multi-line folding; single-quoted with `''` escaping
- **Block scalars** — `|` (literal) and `>` (folded) with chomping indicators (`|-` strip, `|+` keep, `|` clip)
- **Anchors & aliases** — `&name` / `*name` with deep-copy resolution; duplicate/unknown anchors error
- **Merge keys** — `<<: *anchor` splices entries without overwriting explicit keys
- **Tags** — `!tag` recognized and preserved via `node.tag()`; not interpreted
- **Comments** — `pre` (lines before a node) and `inline_` (trailing `# ...`) survive parse → mutate → emit
- **Type conversions** — `as<int>()`, `as<float>()`, `as<double>()`, `as<bool>()`, `as<std::string>()` via `Result<T>` (no exceptions). Bool accepts `true/false/yes/no/on/off/y/n` (case-insensitive); non-bool strings error instead of coercing to `false`.
- **Multi-document streams** — `parseMultiDoc(source, docs)` splits on `---` markers
- **Insertion-order maps** — keys stay in source order, never sorted
- **Error model** — `Result<T>` with structured error codes (`BadIndent`, `UnclosedQuote`, `UnclosedFlow`, `BadEscape`, `UnknownAnchor`, `DuplicateAnchor`, `UnexpectedToken`, `ScalarConversion`, `TypeMismatch`), each carrying line/column/offset

## API

```cpp
import ZYaml;

// Parse — returns Result<YamlDoc>, never throws
auto doc = zyaml::parse("a: 1\nb: [2, 3]\n");
if (!doc) {
    std::cerr << doc.error().format() << "\n";   // "zyaml error at line 1:4: ..."
    return 1;
}
const auto& root = doc->root();

// Lookup (O(1) via transparent hash — no temporary string allocation)
const auto* a = root.find("a");

// Typed conversion (Result<T> — check before use)
auto val = a->as<int>();
if (val) std::cout << *val << "\n";              // 1

// Iterate map entries in insertion order
for (const auto& item : root.items()) {
    std::cout << item.key << ": " << item.value.asString() << "\n";
}

// Iterate sequence elements
for (const auto& e : some_seq.elements()) { /* ... */ }

// Access comments attached at parse time
if (a->comments().inline_) {
    std::cout << "inline: " << *a->comments().inline_ << "\n";
}

// Emit a tree back to YAML (round-trip stable)
std::string yaml = zyaml::emit(root);

// Multi-document streams
std::vector<zyaml::YamlDoc> docs;
if (!zyaml::parseMultiDoc("---\na: 1\n---\nb: 2\n", docs)) {
    std::cout << docs.size() << " documents\n";
}

// Mutation — Node is non-copyable; take a reference
zyaml::Node& mutable_root = doc->root();
mutable_root.remove("a");
mutable_root.appendMapEntry("new", zyaml::Node::makeScalar("value"));

// Deep copy when you need an independent tree
auto copy = root.clone();
```

## Module structure

```
ZYaml (primary unit — `import ZYaml;`)
├── :error    — YamlError, Location, Result<T>
├── :node     — Node, NodeType, Comments
├── :scanner  — Scanner, Token, shared quoted-scalar decoders
├── :parser   — parse(), parseMultiDoc(), YamlDoc
└── :emitter  — emit()
```

Partitions are re-exported via `export import`; consumers only write `import ZYaml;`.

## Building

Requires CMake 4.0+ and either:
- **MSVC 19.44+** (module scanning enabled automatically), or
- **Clang 20+** with `clang-scan-deps` on `PATH` (or set `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS`)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Builds clean with `/W4 /WX` (warnings as errors) on MSVC. The library itself has no runtime dependencies.

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `ZYAML_BUILD_TESTS` | `ON` | Unit, spec, fuzz, and yaml-test-suite tests |
| `ZYAML_BUILD_BENCH` | `OFF` | Performance benchmark (Release-calibrated bounds) |
| `ZYAML_BUILD_EXAMPLES` | `ON` | Example programs |
| `ZYAML_BUILD_FUZZ_DIFF` | `OFF` | Differential fuzzer vs libyaml (fetches libyaml via FetchContent) |

## Testing

Four layers, all run via ctest:

1. **Unit tests** — feature-level coverage (parser, flow, anchors, merge, block scalars, comments, errors, etc.)
2. **Spec tests** (`test_spec`) — YAML 1.2 edge cases (multi-line flow/quoted, colon rules, tab rejection, bracket mismatch, deep nesting)
3. **Curated yaml-test-suite subset** (`test_yaml_suite`) — scenarios citing official test IDs
4. **Property-based fuzz** (`fuzz_harness`) — generates random YAML, checks invariants (no crash, round-trip stability, clone equality, map consistency). Seeded for reproducibility.

Optional, opt-in:

5. **Benchmark** (`zyaml_bench`, `-DZYAML_BUILD_BENCH=ON`) — parse/emit throughput with allocation counts and bytes. Labelled `bench` so CI can run it separately: `ctest -L bench`.
6. **Differential fuzzer** (`fuzz_diff`, `-DZYAML_BUILD_FUZZ_DIFF=ON`) — parses the same input with both ZYaml and libyaml, compares trees. Strict oracle: any structural mismatch or unallowlisted accept/reject asymmetry fails. 0 deviations across 12000 runs.

## Performance

Release build (MSVC 19.44, `/O2`), 400 KB / 20k-key flat map:

| Operation | Time | Throughput | Allocations |
|-----------|------|------------|-------------|
| parse | ~13 ms | ~29 MB/s | 20k (1.0/key) |
| emit | ~5 ms | ~80 MB/s | — |
| round-trip | ~22 ms | — | 40k |

The hash side-index on map storage keeps `find`/`appendMapEntry` O(1) avg; parse is O(n). The allocation column surfaces optimization targets for future arena/zero-copy work.

## Known limitations

- **Comments** — `pre` and `inline_` only; no `post` comment slot
- **Emitter style** — quoting and block-vs-flow are re-derived heuristically; round-trip is structurally equal, not byte-identical
- **No YAML 1.1 type resolution** — dates, timestamps, binary, etc. are not auto-typed; scalars stay strings until `as<T>()` is called
- **Deep nesting** — recursive-descent parser; nesting beyond ~50 levels can overflow the stack
- **Flow-context aliases** — `b: [*a]` does not resolve the alias (stored as the plain string `"*a"`); block-context aliases resolve normally

## License

MIT (see `LICENSE`).
