# ZYaml

A standalone C++23 YAML library — modular, comment-preserving, insertion-ordered, with a `Result<T>` error model.

## Status

M0–M12 complete plus a spec-compliance and performance pass: 8 YAML 1.2 edge-case bugs fixed (multi-line flow/quoted, `a:b` colon rule, tab-indent rejection, root-level flow, nested flow-as-map-value, indented `---`), parse O(n²) eliminated via a hash side-index on map storage, and dead code removed (`TokenStyle`, `Comments::post`, `YamlError::context`). 15 test executables, ~100 test functions — all passing on MSVC 19.44 with `/W4 /WX`. Parse throughput ~26 MB/s (Release) on a 400 KB / 20k-key document.

Not yet integrated into ZeroEngine (M13 is the integration milestone, pending explicit instruction).

## Features

- **Block maps & sequences** with arbitrary nesting
- **Flow style** `[a, b]` and `{k: v}`, including nested flow `[[1,2],[3,4]]` and flow-as-map-value `{x: [1, 2]}`. Multi-line flow collections supported per YAML 1.2.
- **Quoted scalars** — double-quoted with `\n \t \r \\ \" \' \0 \a \b \f \v \e` escapes and multi-line folding (trailing `\` continues, bare newline folds to space); single-quoted with `''` for `'` and multi-line fold-to-space.
- **Type conversions** — `as<int>()`, `as<float>()`, `as<double>()`, `as<bool>()`, `as<std::string>()` via `Result<T>` (no exceptions)
- **Bool** — accepts YAML 1.2 core + 1.1 spellings: `true/false/yes/no/on/off/y/n` (case-insensitive). Non-bool strings return an error, not silent `false`.
- **Null detection** — `null`/`Null`/`NULL`/`~`/empty → `NodeType::Null`
- **Comment preservation** — `pre` (lines before a node) and `inline_` (trailing `# ...` on the node's line) survive parse and are accessible via `node.comments()`
- **Emitter** — `emit(Node)` produces block-style YAML that re-parses to an equal tree (round-trip). Flow collections emitted inline for short scalar-only sequences/maps.
- **Anchors & aliases** — `&name` registers a node; `*name` resolves to a deep copy (independent mutation). Duplicate/unknown anchors error.
- **Merge keys** — `<<: *anchor` splices the referenced map's entries without overwriting explicitly-set keys
- **Tags** — `!tag` recognized and preserved on the node via `node.tag()`; not interpreted
- **Block scalars** — `|` (literal) and `>` (folded) with chomping: `|-` (strip), `|+` (keep), `|` (clip, default). Also in sequence position (`- |`).
- **Multi-document** — `---` separates documents; `parseMultiDoc(source, docs)` returns a vector. `...` (doc end) markers skipped.
- **Error model** — `Result<T>` (variant-based, sidesteps MSVC `std::expected` + modules C2028 bug). Error codes include `BadIndent`, `UnclosedQuote`, `UnclosedFlow`, `BadEscape`, `UnknownAnchor`, `DuplicateAnchor`, `UnexpectedToken`, `ScalarConversion`, `TypeMismatch`. Each carries line/column/offset.
- **Insertion-order maps** — `std::vector<std::pair>` storage, not `std::map`. Keys stay in source order.
- **Node mutation** — `push()`, `appendMapEntry()` (last-key-wins), `remove(key)`, `removeAt(index)`, `clone()` (deep copy). Copy is deleted; move is defaulted.
- **No external dependencies** — pure C++23 modules, no runtime deps.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Builds with `/W4 /WX` (warnings as errors) on MSVC. No external runtime dependencies.

## Known limitations

- **Comments** — only `pre` (lines before a node) and `inline_` (trailing `# ...` on the node's line) are modeled; there is no separate `post` comment slot.
- **Emitter style hints** — the emitter re-derives quoting/block-vs-flow heuristically rather than preserving parse-time style. Round-trip is structurally equal, not byte-identical.
- **No YAML 1.1 type resolution** — dates, timestamps, binary, etc. are not auto-typed; all scalars are strings unless `as<T>()` is called.

## License

MIT (see `LICENSE`).
