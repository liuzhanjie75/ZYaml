// M7 test — emitter (Node tree → YAML text) and round-trip.
//
// Drives: emit(Node) -> std::string producing block-style YAML. The emitted
// text must re-parse to an equal tree (round-trip). Comments (pre/inline)
// must be re-emitted so a comment-bearing tree survives emit→parse.

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

// Recursive deep-equality between two nodes (map/seq/scalar/null).
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

void test_emit_simple_map() {
    auto doc = zyaml::parse("a: 1\nb: 2\n");
    CHECK(doc.has_value(), "parse");
    if (!doc) return;
    std::string out = zyaml::emit(doc->root());
    std::cerr << "  [diag] emitted:\n" << out << "  [diag] reparse: ";
    auto reparsed = zyaml::parse(out);
    if (!reparsed) std::cerr << "ERR " << reparsed.error().format() << "\n";
    else std::cerr << "ok\n";
    CHECK(reparsed.has_value(), "emitted text re-parses");
    if (!reparsed) return;
    CHECK(mapsEqual(doc->root(), reparsed->root()), "round-trip map equal");
}

void test_emit_nested_and_flow() {
    constexpr std::string_view yaml =
        "transform:\n"
        "  position: [1.0, 2.0, 3.0]\n"
        "  scale: [4.0, 5.0, 6.0]\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "parse");
    if (!doc) return;
    std::string out = zyaml::emit(doc->root());
    auto reparsed = zyaml::parse(out);
    if (!reparsed) std::cerr << "  [nested] ERR: " << reparsed.error().format() << "\n";
    CHECK(reparsed.has_value(), "emit nested+flow re-parses");
    if (!reparsed) return;
    CHECK(mapsEqual(doc->root(), reparsed->root()), "round-trip nested+flow");
}

void test_emit_sequence_of_maps() {
    constexpr std::string_view yaml =
        "nodes:\n"
        "  - name: floor\n"
        "    path: Assets/floor.obj\n"
        "  - name: helmet\n"
        "    path: Assets/helmet.obj\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "parse");
    if (!doc) return;
    std::string out = zyaml::emit(doc->root());
    auto reparsed = zyaml::parse(out);
    if (!reparsed) std::cerr << "  [seq] ERR: " << reparsed.error().format() << "\n";
    CHECK(reparsed.has_value(), "emit seq-of-maps re-parses");
    if (!reparsed) return;
    CHECK(mapsEqual(doc->root(), reparsed->root()), "round-trip seq-of-maps");
}

void test_emit_preserves_comments() {
    // A pre comment and an inline comment must survive emit→parse.
    constexpr std::string_view yaml =
        "# header\n"
        "version: 1.0   # schema\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "parse");
    if (!doc) return;
    const auto* v = doc->root().find("version");
    CHECK(v != nullptr, "find version");
    CHECK(v->comments().inline_.has_value(), "version has inline comment");
    const auto& pre = v->comments().pre;
    CHECK(!pre.empty(), "version has pre comment");

    std::string out = zyaml::emit(doc->root());
    auto reparsed = zyaml::parse(out);
    CHECK(reparsed.has_value(), "comment-bearing emit re-parses");
    if (!reparsed) return;
    const auto* v2 = reparsed->root().find("version");
    CHECK(v2 != nullptr, "re-find version");
    CHECK(v2->comments().inline_.has_value(), "inline comment survived round-trip");
    if (v2->comments().inline_) {
        CHECK_EQ(*v2->comments().inline_, std::string("# schema"), "inline text preserved");
    }
    CHECK(!v2->comments().pre.empty(), "pre comment survived round-trip");
    if (!v2->comments().pre.empty()) {
        CHECK_EQ(v2->comments().pre[0], std::string("# header"), "pre text preserved");
    }
}

void test_emit_quoted_string_with_special_chars() {
    // A scalar containing ':' or '#' must be quoted on emit so it re-parses.
    auto doc = zyaml::parse("url: \"https://example.com\"\n");
    CHECK(doc.has_value(), "parse");
    if (!doc) return;
    std::string out = zyaml::emit(doc->root());
    auto reparsed = zyaml::parse(out);
    CHECK(reparsed.has_value(), "quoted-special emit re-parses");
    if (!reparsed) return;
    const auto* url = reparsed->root().find("url");
    CHECK(url != nullptr, "find url");
    CHECK_EQ(std::string(url->asString()), std::string("https://example.com"), "url value preserved");
}

} // namespace

int main() {
    test_emit_simple_map();
    test_emit_nested_and_flow();
    test_emit_sequence_of_maps();
    test_emit_preserves_comments();
    test_emit_quoted_string_with_special_chars();

    if (failures == 0) {
        std::cout << "zyaml M7 emitter tests passed\n";
        return 0;
    }
    std::cerr << failures << " M7 test(s) failed\n";
    return 1;
}
