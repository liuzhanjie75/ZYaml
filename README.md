# ZYaml

A standalone C++23 YAML library — modular, comment-preserving, insertion-ordered, with a `Result<T>` error model.

## Status

M0–M12 complete plus spec-compliance, performance, and test-infrastructure passes: ~15 YAML 1.2 edge-case bugs fixed across scanner/parser/emitter (multi-line flow/quoted, `a:b` colon rule, tab-indent rejection, root-level flow, nested flow-as-map-value, indented `---`, flow bad-escape consistency, bracket-type mismatch, folded block-scalar chomping, empty-collection round-trip, control-char quoting). Parse O(n²) eliminated via a hash side-index on map storage. Dead code removed (`TokenStyle`, `Comments::post`, `YamlError::context`).

18 test executables (19 with the optional differential harness): unit tests, a curated yaml-test-suite subset, property-based fuzz, a release benchmark, and a libyaml differential fuzzer — all passing on MSVC 19.44 with `/W4 /WX`. Parse throughput ~26 MB/s (Release) on a 400 KB / 20k-key document; 0 structural mismatches and 0 unallowlisted accept/reject asymmetries vs libyaml across 12000 differential runs.


## Features

- **Block maps & sequences** with arbitrary nesting
- **Flow style** `[a, b]` and `{k: v}`, including nested flow `[[1,2],[3,4]]` and flow-as-map-value `{x: [1, 2]}`. Multi-line flow collections supported per YAML 1.2.
- **Quoted scalars** — double-quoted with `\n \t \r \\ \" \' \0 \a \b \f \v \e` escapes and multi-line folding (trailing `\` continues, bare newline folds to space); single-quoted with `''` for `'` and multi-line fold-to-space.
- **Type conversions** — `as<int>()`, `as<float>()`, `as<double>()`, `as<bool>()`, `as<std::string>()` via `Result<T>` (no exceptions)
- **Bool** — accepts YAML 1.2 core + 1.1 spellings: `true/false/yes/no/on/off/y/n` (case-insensitive). Non-bool strings return an error, not silent `false`.
- **Null detection** — `null`/`Null`/`NULL`/`~`/empty → `NodeType::Null`
- **Comment preservation** — `pre` (lines before a node) and `inline_` (trailing `# ...` on the node's line) survive parse and are accessible via `node.comments()`
- **Emitter** — `emit(Node)` produces block-style YAML that re-parses to an equal tree (round-trip). Flow collections emitted inline for short scalar-only sequences/maps. Empty collections emit as `[]`/`{}`; scalars containing control chars, leading/trailing whitespace, or quote chars are auto-quoted so they round-trip safely.
- **Anchors & aliases** — `&name` registers a node; `*name` resolves to a deep copy (independent mutation). Duplicate/unknown anchors error.
- **Merge keys** — `<<: *anchor` splices the referenced map's entries without overwriting explicitly-set keys
- **Tags** — `!tag` recognized and preserved on the node via `node.tag()`; not interpreted
- **Block scalars** — `|` (literal) and `>` (folded) with chomping: `|-` (strip), `|+` (keep), `|` (clip, default). Also in sequence position (`- |`).
- **Multi-document** — `---` separates documents; `parseMultiDoc(source, docs)` returns a vector. `...` (doc end) markers skipped.
- **Error model** — `Result<T>` (variant-based, sidesteps MSVC `std::expected` + modules C2028 bug). Error codes include `BadIndent`, `UnclosedQuote`, `UnclosedFlow`, `BadEscape`, `UnknownAnchor`, `DuplicateAnchor`, `UnexpectedToken`, `ScalarConversion`, `TypeMismatch`. Each carries line/column/offset.
- **Insertion-order maps** — `std::vector<std::pair>` storage, not `std::map`. Keys stay in source order.
- **Node mutation** — `push()`, `appendMapEntry()` (last-key-wins), `remove(key)`, `removeAt(index)`, `clone()` (deep copy). Copy is deleted; move is defaulted.
- **No external dependencies** — pure C++23 modules, no runtime deps. (The optional differential fuzzer fetches libyaml at build time — see Testing.)

## API

```cpp
import ZYaml;

// Parse
auto doc = zyaml::parse("a: 1\nb: [2, 3]\n");
if (!doc) {
    std::cerr << doc.error().format() << "\n";
    return;
}
const auto& root = doc->root();

// Access
const auto* a = root.find("a");
auto val = a->as<int>();        // Result<int> — has_value() + operator*
if (val) std::cout << *val << "\n";  // 1

// Iterate (insertion order)
for (const auto& item : root.items()) {
    std::cout << item.key << ": " << item.value.asString() << "\n";
}

// Comments
if (a->comments().inline_) {
    std::cout << "inline: " << *a->comments().inline_ << "\n";
}

// Emit
std::string yaml = zyaml::emit(root);

// Multi-document
std::vector<zyaml::YamlDoc> docs;
auto err = zyaml::parseMultiDoc("---\na: 1\n---\nb: 2\n", docs);
if (!err) {
    std::cout << docs.size() << " documents\n";
}

// Mutation — Node is non-copyable; take a reference, not a copy.
zyaml::Node& mutable_root = doc->root();
mutable_root.remove("a");
mutable_root.appendMapEntry("new", zyaml::Node::makeScalar("value"));
```

## Module structure

```
ZYaml (primary unit — import ZYaml;)
├── :error    — YamlError, Location, Result<T>
├── :node     — Node, NodeType, Comments, convertBool
├── :scanner  — Scanner, Token, readQuoted, readFlowCollection, readBlockScalar
├── :parser   — parse(), parseMultiDoc(), YamlDoc, parseValue, parseBlock, AnchorTable
└── :emitter  — emit()
```

Consumers write `import ZYaml;` and get the full API. Partitions are re-exported via `export import`.

## Building

Requires CMake 4.0+ and either:
- **MSVC 19.44+** (`CMAKE_CXX_SCAN_FOR_MODULES` enabled automatically), or
- **Clang 20+** with `clang-scan-deps` on `PATH` (or set `CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS`).

```sh
# Debug build (runs all tests; benchmark numbers are not meaningful)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

```sh
# Release build (meaningful benchmark timings)
cmake -S . -B build_rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_rel
ctest --test-dir build_rel --output-on-failure
```

Builds with `/W4 /WX` (warnings as errors) on MSVC. The library has no runtime dependencies.

## Testing

Five layers of tests, all run via ctest:

1. **Unit tests** (`test_smoke`, `test_parser`, `test_flow`, `test_anchors`, `test_merge`, `test_block_scalar`, `test_errors`, `test_regression`, `test_convert`, `test_comments`, `test_quoted`, `test_emit`, `test_sequence`, `test_conformance`) — feature-level coverage, one executable per milestone.
2. **Curated yaml-test-suite subset** (`test_yaml_suite`) — 23 scenarios citing official test IDs (2JQS, 3MYT, 4CQQ, 4FJ6, 6JWB, 4ABK, 6PBE, 6ZKB, 6LVF, 4Q9F, 6BFJ, 7LBH, 5C5M, plus error cases). Known-good baseline for implemented features.
3. **Property-based fuzz** (`fuzz_harness`) — generates random YAML and checks invariants (no crash, round-trip stability, clone equality, map consistency). Seeded (`--seed`/`--runs` flags) for reproducibility; ~5000 iterations per ctest run.
4. **Release benchmark** (`zyaml_bench`, opt-in via `-DZYAML_BUILD_BENCH=ON`) — parses/emits five representative shapes and asserts upper bounds to catch performance regressions. Bounds are calibrated for Release; the flat-map bound is the O(n²) sentinel (was 3056 ms before the hash side-index; ~15 ms Release now). Labelled `bench` so CI can run it separately: `ctest -L bench` / `ctest -LE bench`.
5. **Differential fuzzer** (`fuzz_diff`, optional via `-DZYAML_BUILD_FUZZ_DIFF=ON`) — parses the same YAML with both ZYaml and libyaml (the reference implementation) and compares trees. Strict oracle: any accept/reject asymmetry that isn't on the documented allowlist (YAML 1.1 null-key syntax) fails the run. 0 structural mismatches and 0 unallowlisted asymmetries across 12000 runs on 6 seeds.

```sh
# Enable the differential harness (fetches libyaml via FetchContent):
cmake -S . -B build_diff -G Ninja -DZYAML_BUILD_FUZZ_DIFF=ON
cmake --build build_diff --target fuzz_diff
ctest --test-dir build_diff -R fuzz_diff --output-on-failure
```

## Known limitations

- **Comments** — only `pre` (lines before a node) and `inline_` (trailing `# ...` on the node's line) are modeled; there is no separate `post` comment slot.
- **Emitter style hints** — the emitter re-derives quoting/block-vs-flow heuristically rather than preserving parse-time style. Round-trip is structurally equal, not byte-identical.
- **No YAML 1.1 type resolution** — dates, timestamps, binary, etc. are not auto-typed; all scalars are strings unless `as<T>()` is called.
- **Deep nesting** — the parser is recursive-descent; nesting beyond ~50 levels can overflow the stack. Realistic configs stay well under this.

## License

MIT (see `LICENSE`).
