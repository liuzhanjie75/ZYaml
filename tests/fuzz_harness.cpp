// Property-based fuzz harness for ZYaml.
//
// Generates random YAML documents from a grammar, parses them, and checks
// invariants that must hold for any valid parse:
//
//   1. parse() never crashes (it returns Result, never throws).
//   2. If parse(s) succeeds with tree T, then parse(emit(T)) succeeds and
//      produces a structurally equal tree T' (round-trip stability).
//   3. T.clone() is structurally equal to T.
//   4. For every map, every key from items() is findable, and size()
//      matches the item count.
//
// A failure dumps the seed and the input so it can be reproduced. Runs as
// a normal ctest with a fixed seed + iteration count; pass --runs N to
// run longer (for a stress/CI mode).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

import ZYaml;

namespace {

// ── Deterministic PRNG (xorshift64) ────────────────────────────────
// Reproducible from a seed so a failing case can be re-run exactly.
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

// ── Generator ───────────────────────────────────────────────────────
// Recursively emits YAML text. Bias toward valid constructs; occasionally
// inject edge cases (embedded colons, escapes, multi-line).

void genNode(Rng& rng, std::string& out, std::size_t depth);
void genBlockMap(Rng& rng, std::string& out, std::size_t depth, std::size_t indent);
void genBlockSeq(Rng& rng, std::string& out, std::size_t depth, std::size_t indent);

void genString(Rng& rng, std::string& out) {
    // A small alphabet biased toward printable ASCII used in real YAML.
    static const char* alpha =
        "abcdefghijABCDEFGH0123456789 .-_/:%#";
    std::size_t n = rng.range(0, 8);
    for (std::size_t i = 0; i < n; ++i) out.push_back(rng.pick(alpha));
}

void genPlainScalar(Rng& rng, std::string& out) {
    // Sometimes inject an embedded colon (no trailing space) to exercise
    // the plain-scalar colon rule.
    if (rng.chance(1, 5)) {
        out += "key";
        out.push_back(':');
        genString(rng, out);
    } else {
        genString(rng, out);
    }
}

void genDoubleQuoted(Rng& rng, std::string& out) {
    out.push_back('"');
    std::size_t n = rng.range(0, 8);
    for (std::size_t i = 0; i < n; ++i) {
        // Mix plain chars with a few escapes.
        if (rng.chance(1, 4)) {
            const char* escs = "nt\\\"'";
            char e = rng.pick(escs);
            out.push_back('\\'); out.push_back(e);
        } else {
            char c = rng.pick("abcXYZ012 .-");
            out.push_back(c);
        }
    }
    out.push_back('"');
}

void genSingleQuoted(Rng& rng, std::string& out) {
    out.push_back('\'');
    std::size_t n = rng.range(0, 8);
    for (std::size_t i = 0; i < n; ++i) {
        if (rng.chance(1, 6)) {
            // '' escape for a literal quote
            out.push_back('\''); out.push_back('\'');
        } else {
            out.push_back(rng.pick("abcXYZ012 .-"));
        }
    }
    out.push_back('\'');
}

void genScalar(Rng& rng, std::string& out) {
    switch (rng.range(0, 3)) {
        case 0: genPlainScalar(rng, out); break;
        case 1: genDoubleQuoted(rng, out); break;
        case 2: genSingleQuoted(rng, out); break;
        default: genPlainScalar(rng, out); break;
    }
}

void genFlowSeq(Rng& rng, std::string& out, std::size_t depth) {
    out.push_back('[');
    std::size_t n = rng.range(0, 4);
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += ", ";
        if (depth > 0 && rng.chance(1, 3)) {
            // Nested flow
            if (rng.chance(1, 2)) genFlowSeq(rng, out, depth - 1);
            else { out.push_back('{'); out += "k: v"; out.push_back('}'); }
        } else {
            genScalar(rng, out);
        }
    }
    out.push_back(']');
}

void genFlowMap(Rng& rng, std::string& out, std::size_t depth) {
    out.push_back('{');
    std::size_t n = rng.range(0, 4);
    for (std::size_t i = 0; i < n; ++i) {
        if (i) out += ", ";
        genScalar(rng, out);  // key
        out += ": ";
        if (depth > 0 && rng.chance(1, 3)) genFlowSeq(rng, out, depth - 1);
        else genScalar(rng, out);
    }
    out.push_back('}');
}

void genBlockSeq(Rng& rng, std::string& out, std::size_t depth, std::size_t indent) {
    std::size_t n = rng.range(1, 3);
    for (std::size_t i = 0; i < n; ++i) {
        out.append(indent, ' ');
        out += "- ";
        if (depth == 0 || rng.chance(2, 3)) {
            genScalar(rng, out);
        } else if (rng.chance(1, 2)) {
            // Nested block map on the same line + deeper indent
            genScalar(rng, out);  // key
            out += ":\n";
            genBlockMap(rng, out, depth - 1, indent + 2);
            continue;
        } else {
            // Nested block seq
            out.push_back('\n');
            genBlockSeq(rng, out, depth - 1, indent + 2);
            continue;
        }
        out.push_back('\n');
    }
}

void genBlockMap(Rng& rng, std::string& out, std::size_t depth, std::size_t indent) {
    std::size_t n = rng.range(1, 4);
    for (std::size_t i = 0; i < n; ++i) {
        out.append(indent, ' ');
        genScalar(rng, out);  // key (could be plain or quoted)
        out += ":";
        if (depth == 0 || rng.chance(1, 2)) {
            // Inline scalar value
            out.push_back(' ');
            genScalar(rng, out);
            out.push_back('\n');
        } else if (rng.chance(1, 2)) {
            // Inline flow value
            out.push_back(' ');
            if (rng.chance(1, 2)) genFlowSeq(rng, out, depth - 1);
            else genFlowMap(rng, out, depth - 1);
            out.push_back('\n');
        } else {
            // Nested block value
            out.push_back('\n');
            if (rng.chance(1, 2)) genBlockMap(rng, out, depth - 1, indent + 2);
            else genBlockSeq(rng, out, depth - 1, indent + 2);
        }
    }
}

void genNode(Rng& rng, std::string& out, std::size_t depth) {
    // Top-level: mostly block maps (the common case), sometimes flow or seq.
    switch (rng.range(0, 4)) {
        case 0:
        case 1:
        case 2: genBlockMap(rng, out, depth, 0); break;
        case 3: genBlockSeq(rng, out, depth, 0); break;
        default:
            if (rng.chance(1, 2)) genFlowSeq(rng, out, depth);
            else genFlowMap(rng, out, depth);
            out.push_back('\n');
            break;
    }
}

// ── Invariants ──────────────────────────────────────────────────────

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
    if (a.isNull() && b.isNull()) return true;
    if (a.isScalar() && b.isScalar())
        return std::string(a.asString()) == std::string(b.asString());
    if (a.isMap() && b.isMap()) return mapsEqual(a, b);
    if (a.isSequence() && b.isSequence()) return seqsEqual(a, b);
    return false;
}

// Check internal consistency recursively: every map's items() keys are
// findable, and size() matches the iteration count.
bool recurseConsistent(const zyaml::Node& n) {
    if (n.isMap()) {
        std::size_t cnt = 0;
        for (const auto& item : n.items()) {
            ++cnt;
            if (n.find(item.key) == nullptr) return false;
            if (!recurseConsistent(item.value)) return false;
        }
        if (cnt != n.size()) return false;
    } else if (n.isSequence()) {
        for (std::size_t i = 0; i < n.size(); ++i) {
            if (!recurseConsistent(n[i])) return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::uint64_t seed = 0xC0FFEE;
    std::size_t runs = 5000;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 16);
        } else if (std::strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            runs = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        }
    }
    // Incorporate time so different ctest invocations explore different paths.
    // (Seed is still printed so any failure is reproducible.)
    std::printf("fuzz_harness: seed=%llx runs=%zu\n",
                (unsigned long long)seed, runs);

    Rng rng(seed);
    std::size_t failures = 0;
    std::size_t parse_ok = 0;
    std::size_t parse_err = 0;

    for (std::size_t iter = 0; iter < runs; ++iter) {
        std::string yaml;
        genNode(rng, yaml, rng.range(1, 4));

        // Invariant 1: parse never crashes.
        auto doc = zyaml::parse(yaml);
        if (!doc) {
            ++parse_err;
            continue;
        }
        ++parse_ok;
        const auto& root = doc->root();

        // Invariant 4: internal consistency (find/items/size).
        if (!recurseConsistent(root)) {
            std::printf("FAIL consistency iter=%zu seed_step=%llx\n",
                        iter, (unsigned long long)rng.s);
            std::printf("--- input ---\n%s--- end ---\n", yaml.c_str());
            ++failures;
            continue;
        }

        // Invariant 3: clone equals original.
        auto cloned = root.clone();
        if (!nodesEqual(root, cloned)) {
            std::printf("FAIL clone iter=%zu\n", iter);
            std::printf("--- input ---\n%s--- end ---\n", yaml.c_str());
            ++failures;
            continue;
        }

        // Invariant 2: round-trip stable. emit(parse(s)) re-parses to equal tree.
        std::string emitted = zyaml::emit(root);
        auto re = zyaml::parse(emitted);
        if (!re) {
            // emit produced un-parseable text — that's a bug.
            std::printf("FAIL round-trip re-parse iter=%zu err=%s\n",
                        iter, re.error().format().c_str());
            std::printf("--- input ---\n%s--- end ---\n", yaml.c_str());
            std::printf("--- emitted ---\n%s--- end ---\n", emitted.c_str());
            ++failures;
            continue;
        }
        if (!nodesEqual(root, re->root())) {
            std::printf("FAIL round-trip structural iter=%zu\n", iter);
            std::printf("--- input ---\n%s--- end ---\n", yaml.c_str());
            std::printf("--- emitted ---\n%s--- end ---\n", emitted.c_str());
            ++failures;
        }
    }

    std::printf("fuzz done: ok=%zu err=%zu failures=%zu\n",
                parse_ok, parse_err, failures);
    return failures ? 1 : 0;
}
