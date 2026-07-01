// Differential fuzz harness — parses the same YAML with ZYaml and libyaml,
// compares the resulting trees. Catches spec deviations that property-based
// fuzzing (which only checks internal invariants) would miss.
//
// libyaml is the reference implementation (used by PyYAML, libyaml-cpp
// upstream, etc.). Built as a test-only dependency via FetchContent, gated
// on ZYAML_BUILD_FUZZ_DIFF so the library itself stays dep-free.
//
// The comparison is on structural + scalar-string equality. libyaml
// resolves tags (ints, bools, nulls) to plain scalars in event form, so we
// compare scalar values as strings — matching ZYaml's "everything is a
// string until as<T>() is called" model. Anchors/aliases are resolved
// (cloned) by both, so alias nodes should produce equal subtrees.

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <yaml.h>

import ZYaml;

using namespace zyaml;

namespace {

// ── Deterministic PRNG (same as fuzz_harness) ──────────────────────
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
    std::uint64_t next() {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return s;
    }
    std::size_t range(std::size_t lo, std::size_t hi) {
        if (hi <= lo) return lo;
        return lo + next() % (hi - lo + 1);
    }
    bool chance(std::size_t num, std::size_t den) {
        return den == 0 ? false : (next() % den) < num;
    }
    char pick(const char* opts) {
        std::size_t n = std::strlen(opts);
        return opts[range(0, n - 1)];
    }
};

// ── Generator (subset of fuzz_harness — block maps, seqs, flow, scalars) ──
void genNode(Rng& rng, std::string& out, std::size_t depth);
void genBlockMap(Rng& rng, std::string& out, std::size_t depth, std::size_t indent);
void genBlockSeq(Rng& rng, std::string& out, std::size_t depth, std::size_t indent);

void genString(Rng& rng, std::string& out) {
    static const char* alpha = "abcdefghij0123456789 .-";
    std::size_t n = rng.range(0, 6);
    for (std::size_t i = 0; i < n; ++i) out.push_back(rng.pick(alpha));
}

void genScalar(Rng& rng, std::string& out) {
    // Plain or double-quoted only — keeps the comparison simple.
    if (rng.chance(1, 3)) {
        out.push_back('"');
        std::size_t n = rng.range(0, 6);
        for (std::size_t i = 0; i < n; ++i) {
            if (rng.chance(1, 4)) {
                out.push_back('\\');
                out.push_back(rng.pick("nt\\\""));
            } else {
                out.push_back(rng.pick("abcXYZ012 .-"));
            }
        }
        out.push_back('"');
    } else {
        genString(rng, out);
    }
}

void genBlockMap(Rng& rng, std::string& out, std::size_t depth, std::size_t indent) {
    std::size_t n = rng.range(1, 3);
    for (std::size_t i = 0; i < n; ++i) {
        out.append(indent, ' ');
        genScalar(rng, out);  // key
        out += ":";
        if (depth == 0 || rng.chance(1, 2)) {
            out.push_back(' ');
            genScalar(rng, out);
            out.push_back('\n');
        } else {
            out.push_back('\n');
            genBlockMap(rng, out, depth - 1, indent + 2);
        }
    }
}

void genBlockSeq(Rng& rng, std::string& out, std::size_t depth, std::size_t indent) {
    std::size_t n = rng.range(1, 3);
    for (std::size_t i = 0; i < n; ++i) {
        out.append(indent, ' ');
        out += "- ";
        if (depth == 0 || rng.chance(2, 3)) {
            genScalar(rng, out);
        } else {
            genScalar(rng, out);
            out += ":\n";
            genBlockMap(rng, out, depth - 1, indent + 2);
            continue;
        }
        out.push_back('\n');
    }
}

void genNode(Rng& rng, std::string& out, std::size_t depth) {
    switch (rng.range(0, 2)) {
        case 0: genBlockMap(rng, out, depth, 0); break;
        case 1: genBlockSeq(rng, out, depth, 0); break;
        default: genBlockMap(rng, out, depth, 0); break;
    }
}

// ── libyaml → ZYaml Node adapter ───────────────────────────────────
// Builds a ZYaml Node tree from libyaml events using a stack. On a
// container-start, push a new frame. On container-end, pop the frame and
// attach its completed node to the new top-of-stack (or set as root if
// the stack is empty). Scalars attach to the current top-of-stack.

Result<Node> libyamlToNode(const std::string& yaml_str) {
    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        return YamlError{YamlErrorCode::IoError, {}, "libyaml init failed"};
    }
    yaml_parser_set_input_string(&parser,
        reinterpret_cast<const unsigned char*>(yaml_str.data()),
        yaml_str.size());

    std::unordered_map<std::string, Node> anchors;
    struct Frame {
        zyaml::Node node;
        std::string pending_key;
        bool has_pending_key = false;
    };
    std::vector<Frame> stack;
    zyaml::Node root = zyaml::Node::makeNull();
    bool got_root = false;

    // Attach a completed child node to the current top-of-stack (or set
    // as root if the stack is empty).
    auto attach = [&](zyaml::Node child) {
        if (stack.empty()) {
            root = std::move(child);
            got_root = true;
            return;
        }
        Frame& parent = stack.back();
        if (parent.node.isMap()) {
            if (!parent.has_pending_key) {
                // This scalar is a key — stash it, value comes next.
                parent.pending_key = std::string(child.asString());
                parent.has_pending_key = true;
            } else {
                parent.node.appendMapEntry(std::move(parent.pending_key), std::move(child));
                parent.has_pending_key = false;
            }
        } else {
            parent.node.appendSeqElement(std::move(child));
        }
    };

    yaml_event_t event;
    bool ok = true;
    std::string err_msg;
    bool done = false;
    while (!done && ok) {
        if (!yaml_parser_parse(&parser, &event)) {
            ok = false;
            err_msg = "libyaml parse error";
            break;
        }
        switch (event.type) {
            case YAML_STREAM_START_EVENT:
            case YAML_STREAM_END_EVENT:
            case YAML_DOCUMENT_START_EVENT:
            case YAML_DOCUMENT_END_EVENT:
                break;
            case YAML_MAPPING_START_EVENT: {
                zyaml::Node m = zyaml::Node::makeMap();
                if (event.data.mapping_start.anchor) {
                    std::string a(reinterpret_cast<const char*>(event.data.mapping_start.anchor));
                    anchors.emplace(a, m.clone());
                }
                stack.push_back(Frame{std::move(m), {}, false});
                break;
            }
            case YAML_SEQUENCE_START_EVENT: {
                zyaml::Node s = zyaml::Node::makeSequence();
                if (event.data.sequence_start.anchor) {
                    std::string a(reinterpret_cast<const char*>(event.data.sequence_start.anchor));
                    anchors.emplace(a, s.clone());
                }
                stack.push_back(Frame{std::move(s), {}, false});
                break;
            }
            case YAML_MAPPING_END_EVENT:
            case YAML_SEQUENCE_END_EVENT: {
                if (stack.empty()) break;
                Frame f = std::move(stack.back());
                stack.pop_back();
                attach(std::move(f.node));
                break;
            }
            case YAML_SCALAR_EVENT: {
                std::string val(reinterpret_cast<const char*>(event.data.scalar.value),
                                event.data.scalar.length);
                zyaml::Node n = zyaml::Node::makeScalarOrNull(val);
                if (event.data.scalar.anchor) {
                    std::string a(reinterpret_cast<const char*>(event.data.scalar.anchor));
                    anchors.emplace(a, n.clone());
                }
                attach(std::move(n));
                break;
            }
            case YAML_ALIAS_EVENT: {
                std::string a(reinterpret_cast<const char*>(event.data.alias.anchor));
                auto it = anchors.find(a);
                zyaml::Node n = (it != anchors.end()) ? it->second.clone()
                                                       : zyaml::Node::makeNull();
                attach(std::move(n));
                break;
            }
            default:
                break;
        }
        done = (event.type == YAML_STREAM_END_EVENT);
        yaml_event_delete(&event);
    }
    yaml_parser_delete(&parser);
    if (!ok) {
        return YamlError{YamlErrorCode::IoError, {}, err_msg};
    }
    if (!got_root) return zyaml::Node::makeNull();
    return root;
}

// ── Tree comparison ────────────────────────────────────────────────
bool nodesEqual(const zyaml::Node& a, const zyaml::Node& b);

bool mapsEqual(const zyaml::Node& a, const zyaml::Node& b) {
    if (!a.isMap() || !b.isMap()) return false;
    if (a.size() != b.size()) return false;
    for (const auto& item : a.items()) {
        const auto* bv = b.find(item.key);
        if (!bv) return false;
        if (!nodesEqual(item.value, *bv)) return false;
    }
    return true;
}

bool seqsEqual(const zyaml::Node& a, const zyaml::Node& b) {
    if (!a.isSequence() || !b.isSequence()) return false;
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!nodesEqual(a[i], b[i])) return false;
    }
    return true;
}

bool nodesEqual(const zyaml::Node& a, const zyaml::Node& b) {
    // Both should present null the same way. libyaml emits null scalars
    // as empty strings or "null"/"~"; ZYaml converts those to Null.
    if (a.isNull() && b.isNull()) return true;
    if (a.isNull() || b.isNull()) {
        // One is null, the other is a scalar — only equal if the scalar
        // is also a null spelling (already converted).
        return false;
    }
    if (a.isScalar() && b.isScalar())
        return std::string(a.asString()) == std::string(b.asString());
    if (a.isMap() && b.isMap()) return mapsEqual(a, b);
    if (a.isSequence() && b.isSequence()) return seqsEqual(a, b);
    return false;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 0xFEED;
    std::size_t runs = 2000;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = std::strtoull(argv[++i], nullptr, 16);
        else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
            runs = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
    }
    std::printf("fuzz_diff: seed=%llx runs=%zu\n", (unsigned long long)seed, runs);

    Rng rng(seed);
    std::size_t both_ok = 0, both_err = 0, zyaml_only_err = 0, libyaml_only_err = 0;
    std::size_t mismatches = 0;

    for (std::size_t iter = 0; iter < runs; ++iter) {
        std::string yaml;
        genNode(rng, yaml, rng.range(1, 3));

        auto zdoc = zyaml::parse(yaml);
        auto lnode = libyamlToNode(yaml);

        if (!zdoc && !lnode) {
            ++both_err;
            continue;
        }
        if (zdoc && lnode) {
            ++both_ok;
            if (!nodesEqual(zdoc->root(), *lnode)) {
                if (mismatches < 10) {
                    std::printf("MISMATCH iter=%zu\n", iter);
                    std::printf("--- input ---\n%s--- end ---\n", yaml.c_str());
                }
                ++mismatches;
            }
            continue;
        }
        if (!zdoc) { ++zyaml_only_err; continue; }
        ++libyaml_only_err;
    }

    std::printf("fuzz_diff done: both_ok=%zu both_err=%zu "
                "zyaml_only_err=%zu libyaml_only_err=%zu mismatches=%zu\n",
                both_ok, both_err, zyaml_only_err, libyaml_only_err, mismatches);
    return mismatches ? 1 : 0;
}
