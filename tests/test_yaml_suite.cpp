// Curated subset of the official yaml-test-suite scenarios.
//
// Each case cites the official test ID it corresponds to and inlines the
// YAML input (faithful to the suite's documented scenario). The driver
// checks parse success/failure and structural properties — establishing
// a compliance baseline for the features ZYaml implements.
//
// Cases that exercise unimplemented features (YAML 1.1 type resolution,
// complex keys, exotic line breaks, BOM) are intentionally excluded —
// this is a known-good baseline, not a whitelist of failures.
//
// Source: https://github.com/yaml/yaml-test-suite (MIT)

#include <iostream>
#include <string>
#include <string_view>

import ZYaml;

namespace {

int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } } while(0)

#define CHECK_EQ(a, b, msg) \
    do { auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { std::cerr << "FAIL: " << (msg) << " (got " << _va << ", want " << _vb << ")\n"; ++failures; } } while(0)

// Run one case: parse `yaml`, expect success, call `check` on the root.
void run_ok(const char* id, std::string_view yaml,
            void (*check)(const zyaml::Node&)) {
    auto doc = zyaml::parse(yaml);
    if (!doc) {
        std::cerr << "FAIL [" << id << "]: parse error: " << doc.error().format() << "\n";
        ++failures;
        return;
    }
    check(doc->root());
}

// Run one case: parse `yaml`, expect failure with `code`.
void run_err(const char* id, std::string_view yaml, zyaml::YamlErrorCode code) {
    auto doc = zyaml::parse(yaml);
    if (doc.has_value()) {
        std::cerr << "FAIL [" << id << "]: expected error code "
                  << static_cast<int>(code) << " but parse succeeded\n";
        ++failures;
        return;
    }
    if (doc.error().code != code) {
        std::cerr << "FAIL [" << id << "]: wrong error code (got "
                  << static_cast<int>(doc.error().code) << ", want "
                  << static_cast<int>(code) << ")\n";
        ++failures;
    }
}

const zyaml::Node* at(const zyaml::Node& n, std::string_view key) {
    return n.find(key);
}

// ── Block maps & sequences ──────────────────────────────────────────

// 2JQS: Block mapping (basic multi-key)
void t_2JQS(const zyaml::Node& r) {
    CHECK(r.isMap(), "2JQS: root is map");
    CHECK_EQ(r.size(), 2u, "2JQS: 2 keys");
    const auto* name = at(r, "name");
    CHECK(name != nullptr, "2JQS: find name");
    CHECK_EQ(std::string(name->asString()), "Bob", "2JQS: name value");
    const auto* age = at(r, "age");
    CHECK(age != nullptr, "2JQS: find age");
    CHECK_EQ(*age->as<int>(), 30, "2JQS: age value");
}
void test_2JQS() {
    run_ok("2JQS", "name: Bob\nage: 30\n", t_2JQS);
}

// 3MYT: Block sequence at root
void t_3MYT(const zyaml::Node& r) {
    CHECK(r.isSequence(), "3MYT: root is seq");
    CHECK_EQ(r.size(), 3u, "3MYT: 3 items");
    CHECK_EQ(std::string(r[0].asString()), "one", "3MYT: item 0");
}
void test_3MYT() {
    run_ok("3MYT", "- one\n- two\n- three\n", t_3MYT);
}

// 4CQQ: Flow sequence in block context
void t_4CQQ(const zyaml::Node& r) {
    const auto* items = at(r, "items");
    CHECK(items != nullptr && items->isSequence(), "4CQQ: items is seq");
    CHECK_EQ(items->size(), 3u, "4CQQ: 3 items");
    CHECK_EQ(std::string((*items)[0].asString()), "a", "4CQQ: item 0");
}
void test_4CQQ() {
    run_ok("4CQQ", "items: [a, b, c]\n", t_4CQQ);
}

// 4FJ6: Flow mapping with nested entries
void t_4FJ6(const zyaml::Node& r) {
    const auto* m = at(r, "point");
    CHECK(m != nullptr && m->isMap(), "4FJ6: point is map");
    CHECK_EQ(m->size(), 3u, "4FJ6: 3 keys");
    CHECK_EQ(*m->find("x")->as<int>(), 1, "4FJ6: x == 1");
    CHECK_EQ(*m->find("y")->as<int>(), 2, "4FJ6: y == 2");
}
void test_4FJ6() {
    run_ok("4FJ6", "point: {x: 1, y: 2, z: 3}\n", t_4FJ6);
}

// ── Anchors & aliases ──────────────────────────────────────────────

// 6JWB: Anchors and aliases on scalars
void t_6JWB(const zyaml::Node& r) {
    const auto* a = at(r, "a");
    CHECK(a != nullptr, "6JWB: find a");
    CHECK_EQ(std::string(a->asString()), "value", "6JWB: a value");
    const auto* b = at(r, "b");
    CHECK(b != nullptr, "6JWB: find b (alias)");
    CHECK_EQ(std::string(b->asString()), "value", "6JWB: b == a via alias");
}
void test_6JWB() {
    run_ok("6JWB", "a: &a value\nb: *a\n", t_6JWB);
}

// 4ABK: Merge key with override
void t_4ABK(const zyaml::Node& r) {
    const auto* d = at(r, "derived");
    CHECK(d != nullptr && d->isMap(), "4ABK: derived is map");
    CHECK_EQ(*d->find("x")->as<int>(), 1, "4ABK: merged x == 1");
    CHECK_EQ(*d->find("y")->as<int>(), 2, "4ABK: merged y == 2");
    CHECK_EQ(*d->find("z")->as<int>(), 9, "4ABK: override z == 9 (not 3)");
}
void test_4ABK() {
    run_ok("4ABK",
        "base: &base\n"
        "  x: 1\n"
        "  y: 2\n"
        "  z: 3\n"
        "derived:\n"
        "  <<: *base\n"
        "  z: 9\n",
        t_4ABK);
}

// ── Tags ───────────────────────────────────────────────────────────

// 6PBE: Local tag preserved (not interpreted)
void t_6PBE(const zyaml::Node& r) {
    const auto* v = at(r, "value");
    CHECK(v != nullptr, "6PBE: find value");
    CHECK(v->tag().has_value(), "6PBE: tag preserved");
    if (v->tag()) CHECK_EQ(*v->tag(), std::string("!str"), "6PBE: tag text");
    CHECK_EQ(std::string(v->asString()), "hello", "6PBE: value");
}
void test_6PBE() {
    run_ok("6PBE", "value: !str hello\n", t_6PBE);
}

// ── Block scalars ──────────────────────────────────────────────────

// 6ZKB: Literal block scalar with strip (|-)
void t_6ZKB(const zyaml::Node& r) {
    const auto* t = at(r, "text");
    CHECK(t != nullptr, "6ZKB: find text");
    CHECK_EQ(std::string(t->asString()), "line one\nline two", "6ZKB: stripped value");
}
void test_6ZKB() {
    run_ok("6ZKB",
        "text: |-\n"
        "  line one\n"
        "  line two\n",
        t_6ZKB);
}

// 6LVF: Folded block scalar with keep (>+)
void t_6LVF(const zyaml::Node& r) {
    const auto* t = at(r, "text");
    CHECK(t != nullptr, "6LVF: find text");
    // Folded: lines joined by space; keep preserves trailing newlines.
    std::string want = "folded line one folded line two\n";
    CHECK_EQ(std::string(t->asString()), want, "6LVF: folded+keep value");
}
void test_6LVF() {
    run_ok("6LVF",
        "text: >+\n"
        "  folded line one\n"
        "  folded line two\n",
        t_6LVF);
}

// ── Quoted scalars ─────────────────────────────────────────────────

// 4Q9F: Double-quoted scalar with escapes
void t_4Q9F(const zyaml::Node& r) {
    const auto* s = at(r, "s");
    CHECK(s != nullptr, "4Q9F: find s");
    std::string want = "line1\nline2\t\"q\"";
    CHECK_EQ(std::string(s->asString()), want, "4Q9F: decoded value");
}
void test_4Q9F() {
    run_ok("4Q9F", "s: \"line1\\nline2\\t\\\"q\\\"\"\n", t_4Q9F);
}

// 6BFJ: Single-quoted scalar with '' escape
void t_6BFJ(const zyaml::Node& r) {
    const auto* s = at(r, "s");
    CHECK(s != nullptr, "6BFJ: find s");
    CHECK_EQ(std::string(s->asString()), "it's here", "6BFJ: sq escape");
}
void test_6BFJ() {
    run_ok("6BFJ", "s: 'it''s here'\n", t_6BFJ);
}

// ── Comments ───────────────────────────────────────────────────────

// 7LBH: Comment lines in various positions
void t_7LBH(const zyaml::Node& r) {
    const auto* a = at(r, "a");
    CHECK(a != nullptr, "7LBH: find a");
    CHECK_EQ(std::string(a->asString()), "1", "7LBH: a value");
    // Pre-comment on b
    const auto* b = at(r, "b");
    CHECK(b != nullptr, "7LBH: find b");
    CHECK(!b->comments().pre.empty(), "7LBH: b has pre comment");
}
void test_7LBH() {
    run_ok("7LBH",
        "# header\n"
        "a: 1   # inline\n"
        "# before b\n"
        "b: 2\n",
        t_7LBH);
}

// ── Multi-document ─────────────────────────────────────────────────

// 5C5M: Multi-document stream with separators
void test_5C5M() {
    std::vector<zyaml::YamlDoc> docs;
    auto err = zyaml::parseMultiDoc(
        "a: 1\n---\nb: 2\n---\nc: 3\n", docs);
    CHECK(!err.has_value(), "5C5M: multi-doc parse ok");
    if (err) return;
    CHECK_EQ(docs.size(), 3u, "5C5M: 3 documents");
    if (docs.size() < 3) return;
    CHECK(docs[0].root().find("a") != nullptr, "5C5M: doc 0 has a");
    CHECK(docs[1].root().find("b") != nullptr, "5C5M: doc 1 has b");
    CHECK(docs[2].root().find("c") != nullptr, "5C5M: doc 2 has c");
}

// ── Error cases ────────────────────────────────────────────────────

// 2SXE: Bad indentation in block mapping
void test_2SXE() {
    run_err("2SXE", "a: 1\n b: 2\n", zyaml::YamlErrorCode::BadIndent);
}

// 82AN: Unclosed flow sequence
void test_82AN() {
    run_err("82AN", "x: [1, 2\n", zyaml::YamlErrorCode::UnclosedFlow);
}

// Unclosed flow map
void test_82AN_map() {
    run_err("82ANm", "x: {a: 1\n", zyaml::YamlErrorCode::UnclosedFlow);
}

// Unclosed quote
void test_unclosed_quote() {
    run_err("UQ", "s: \"unterminated\n", zyaml::YamlErrorCode::UnclosedQuote);
}

// Unknown anchor
void test_unknown_anchor() {
    run_err("UA", "x: *missing\n", zyaml::YamlErrorCode::UnknownAnchor);
}

// Duplicate anchor
void test_dup_anchor() {
    run_err("DA", "a: &x 1\nb: &x 2\n", zyaml::YamlErrorCode::DuplicateAnchor);
}

// Mixed map/seq at same indent
void test_mixed_map_seq() {
    run_err("MX", "a: 1\n- lost\n", zyaml::YamlErrorCode::UnexpectedToken);
}

// Tab indentation
void test_tab_indent() {
    run_err("TAB", "a:\n\tb: 1\n", zyaml::YamlErrorCode::BadIndent);
}

// Bad escape
void test_bad_escape() {
    run_err("BE", "s: \"bad \\q\"\n", zyaml::YamlErrorCode::BadEscape);
}

// ── Round-trip stability on real-world shapes ──────────────────────

// Round-trip a scene-file-shaped document (the ZeroEngine use case).
void test_scene_round_trip() {
    constexpr std::string_view yaml =
        "# scene config\n"
        "camera:\n"
        "  fov: 60\n"
        "  near: 0.1\n"
        "lights:\n"
        "  - type: directional   # sun\n"
        "    intensity: 3.0\n"
        "  - type: ambient\n"
        "    intensity: 0.1\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "scene: parse ok");
    if (!doc) return;
    std::string emitted = zyaml::emit(doc->root());
    auto re = zyaml::parse(emitted);
    CHECK(re.has_value(), "scene: round-trip re-parse ok");
    if (!re) return;
    CHECK(re->root().find("camera") != nullptr, "scene: rt camera");
    CHECK(re->root().find("lights") != nullptr, "scene: rt lights");
    CHECK_EQ(re->root().find("lights")->size(), 2u, "scene: rt 2 lights");
}

} // namespace

int main() {
    test_2JQS();
    test_3MYT();
    test_4CQQ();
    test_4FJ6();
    test_6JWB();
    test_4ABK();
    test_6PBE();
    test_6ZKB();
    test_6LVF();
    test_4Q9F();
    test_6BFJ();
    test_7LBH();
    test_5C5M();
    test_2SXE();
    test_82AN();
    test_82AN_map();
    test_unclosed_quote();
    test_unknown_anchor();
    test_dup_anchor();
    test_mixed_map_seq();
    test_tab_indent();
    test_bad_escape();
    test_scene_round_trip();

    if (failures == 0) {
        std::cout << "zyaml curated yaml-test-suite subset passed\n";
        return 0;
    }
    std::cerr << failures << " suite test(s) failed\n";
    return 1;
}
