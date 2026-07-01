// M3 test — flow style [a, b] and {k: v}, mixed with block.
//
// Drives: flow sequences and flow maps, including the float3 shape the
// scene files actually use (color: [0.28, 0.38, 0.62]).

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

import ZYaml;

namespace {

int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } } while(0)

#define CHECK_EQ(a, b, msg) \
    do { auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { std::cerr << "FAIL: " << (msg) << " (got " << _va << ", want " << _vb << ")\n"; ++failures; } } while(0)

void test_flow_sequence() {
    constexpr std::string_view yaml = "items: [apple, banana, cherry]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [flow-seq] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "flow-seq parse should succeed");
    if (!doc) return;
    const auto* items = doc->root().find("items");
    CHECK(items != nullptr, "find items");
    CHECK(items->isSequence(), "items should be a sequence");
    CHECK_EQ(items->size(), 3u, "items size");
    CHECK_EQ(std::string((*items)[0].asString()), "apple", "elem 0");
    CHECK_EQ(std::string((*items)[1].asString()), "banana", "elem 1");
    CHECK_EQ(std::string((*items)[2].asString()), "cherry", "elem 2");
}

void test_flow_map() {
    constexpr std::string_view yaml = "point: {x: 1, y: 2, z: 3}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [flow-map] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "flow-map parse should succeed");
    if (!doc) return;
    const auto* point = doc->root().find("point");
    CHECK(point != nullptr, "find point");
    CHECK(point->isMap(), "point should be a map");
    CHECK_EQ(point->size(), 3u, "point size");
    auto x = point->find("x")->as<int>();
    CHECK(x.has_value(), "x as<int>");
    CHECK_EQ(*x, 1, "x == 1");
    auto y = point->find("y")->as<int>();
    CHECK_EQ(*y, 2, "y == 2");
    auto z = point->find("z")->as<int>();
    CHECK_EQ(*z, 3, "z == 3");
}

void test_float3_flow_sequence() {
    // The exact shape ZeroEngine's scene files use for vectors.
    constexpr std::string_view yaml = "color: [0.28, 0.38, 0.62]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [float3] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "float3 parse should succeed");
    if (!doc) return;
    const auto* color = doc->root().find("color");
    CHECK(color != nullptr, "find color");
    CHECK(color->isSequence(), "color should be a sequence");
    CHECK_EQ(color->size(), 3u, "color size");
    // M3 only has int/string conversion; float arrives in M4. Verify the
    // scalar text round-trips for now.
    CHECK_EQ(std::string((*color)[0].asString()), "0.28", "color[0] text");
    CHECK_EQ(std::string((*color)[1].asString()), "0.38", "color[1] text");
    CHECK_EQ(std::string((*color)[2].asString()), "0.62", "color[2] text");
}

void test_flow_inside_block() {
    // Block map with a flow sequence value — the real scene shape.
    constexpr std::string_view yaml =
        "transform:\n"
        "  position: [1.0, 2.0, 3.0]\n"
        "  scale: [4.0, 5.0, 6.0]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [flow-in-block] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "flow-in-block parse should succeed");
    if (!doc) return;
    const auto* tf = doc->root().find("transform");
    CHECK(tf != nullptr, "find transform");
    CHECK(tf->isMap(), "transform is map");
    const auto* pos = tf->find("position");
    CHECK(pos != nullptr, "find position");
    CHECK(pos->isSequence(), "position is seq");
    CHECK_EQ(pos->size(), 3u, "position size");
    CHECK_EQ(std::string((*pos)[0].asString()), "1.0", "pos[0]");
    const auto* scale = tf->find("scale");
    CHECK(scale != nullptr, "find scale");
    CHECK_EQ(scale->size(), 3u, "scale size");
    CHECK_EQ(std::string((*scale)[2].asString()), "6.0", "scale[2]");
}

void test_nested_flow_in_map() {
    // Flow map whose value is a nested flow collection — the bug that
    // previously reported "flow map entry missing ':'" because the comma
    // splitter didn't track bracket depth.
    constexpr std::string_view yaml =
        "m: {x: [1, 2], y: {a: 1}}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [nested] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "nested flow in map parse should succeed");
    if (!doc) return;
    const auto* m = doc->root().find("m");
    CHECK(m != nullptr && m->isMap(), "m is a map");
    CHECK_EQ(m->size(), 2u, "m has 2 keys");
    const auto* x = m->find("x");
    CHECK(x != nullptr && x->isSequence(), "m.x is seq");
    CHECK_EQ(x->size(), 2u, "m.x has 2 elems");
    CHECK_EQ(std::string((*x)[0].asString()), "1", "m.x[0]");
    CHECK_EQ(std::string((*x)[1].asString()), "2", "m.x[1]");
    const auto* y = m->find("y");
    CHECK(y != nullptr && y->isMap(), "m.y is map");
    CHECK_EQ(y->size(), 1u, "m.y has 1 key");
    const auto* a = y->find("a");
    CHECK(a != nullptr, "m.y.a present");
    if (a) CHECK_EQ(std::string(a->asString()), "1", "m.y.a == 1");
}

void test_nested_flow_in_seq() {
    // Flow seq containing flow maps: [{a: 1}, {b: 2}].
    constexpr std::string_view yaml = "list: [{a: 1}, {b: 2}]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [nested-seq] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "nested flow in seq parse should succeed");
    if (!doc) return;
    const auto* list = doc->root().find("list");
    CHECK(list != nullptr && list->isSequence(), "list is seq");
    CHECK_EQ(list->size(), 2u, "list has 2 maps");
    CHECK((*list)[0].isMap(), "list[0] is map");
    CHECK_EQ(std::string((*list)[0].find("a")->asString()), "1", "list[0].a");
    CHECK_EQ(std::string((*list)[1].find("b")->asString()), "2", "list[1].b");
}

void test_flow_map_quoted_key_with_colon() {
    // A quoted key containing ':' must NOT be split at the embedded colon.
    // Previously item.find(':') split "a:b":1 into key="a val="b":1.
    constexpr std::string_view yaml = "m: {\"a:b\": 1}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [quoted-key] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "quoted-key flow map parse should succeed");
    if (!doc) return;
    const auto* m = doc->root().find("m");
    CHECK(m != nullptr && m->isMap(), "m is map");
    CHECK_EQ(m->size(), 1u, "m has 1 key");
    const auto* v = m->find("a:b");
    CHECK(v != nullptr, "key 'a:b' present (embedded colon preserved)");
    if (v) CHECK_EQ(std::string(v->asString()), "1", "value == 1");
}

void test_flow_map_quoted_key_value_with_colon() {
    // Both key and value contain ':' inside quotes — the separator must be
    // the top-level colon between them, not the embedded ones.
    constexpr std::string_view yaml = "m: {\"a:b\": \"c:d\"}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [quoted-kv] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "quoted-kv flow map parse should succeed");
    if (!doc) return;
    const auto* m = doc->root().find("m");
    const auto* v = m->find("a:b");
    CHECK(v != nullptr, "key 'a:b' present");
    if (v) CHECK_EQ(std::string(v->asString()), "c:d", "value 'c:d' preserved");
}

void test_flow_map_plain_key_with_embedded_colon() {
    // {a:b: 1} — "a:b" is a plain scalar key (colon not followed by
    // whitespace), so the separator is the second ':'. Previously
    // findTopLevelColon split at the first ':', giving key="a" val="b: 1".
    constexpr std::string_view yaml = "m: {a:b: 1}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [colon-key] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "plain-key-with-colon parse should succeed");
    if (!doc) return;
    const auto* m = doc->root().find("m");
    CHECK_EQ(m->size(), 1u, "1 entry");
    const auto* v = m->find("a:b");
    CHECK(v != nullptr, "key 'a:b' present (embedded colon in plain key)");
    if (v) CHECK_EQ(std::string(v->asString()), "1", "value == 1");
}

void test_flow_multiline_quoted_matches_top_level() {
    // A multi-line double-quoted scalar must fold identically whether it
    // appears at top level or inside a flow collection. Previously the flow
    // path didn't fold, leaving a literal newline in the value.
    constexpr std::string_view top = R"(s: "a
 b")";
    constexpr std::string_view flow = R"(s: ["a
 b"])";
    auto d1 = zyaml::parse(top);
    auto d2 = zyaml::parse(flow);
    CHECK(d1.has_value(), "top-level multiline dq parse");
    CHECK(d2.has_value(), "flow multiline dq parse");
    if (!d1 || !d2) return;
    const auto* t = d1->root().find("s");
    const auto* f = d2->root().find("s");
    CHECK(t != nullptr && t->isScalar(), "top is scalar");
    CHECK(f != nullptr && f->isSequence(), "flow is seq");
    if (!t || !f || f->size() < 1) return;
    CHECK_EQ(std::string(t->asString()), std::string("a b"), "top folds to 'a b'");
    CHECK_EQ(std::string((*f)[0].asString()), std::string("a b"),
             "flow folds to 'a b' (matches top level)");
}

void test_flow_quoted_continuation() {
    // Trailing backslash continuation inside a flow collection — must
    // fold to nothing, matching top-level behavior.
    constexpr std::string_view top = R"(s: "abc\
    def")";
    constexpr std::string_view flow = R"(s: ["abc\
    def"])";
    auto d1 = zyaml::parse(top);
    auto d2 = zyaml::parse(flow);
    if (!d1 || !d2) { CHECK(false, "continuation parse failed"); return; }
    const auto* t = d1->root().find("s");
    const auto* f = d2->root().find("s");
    if (!t || !f || f->size() < 1) return;
    CHECK_EQ(std::string(t->asString()), std::string("abcdef"), "top continuation");
    CHECK_EQ(std::string((*f)[0].asString()), std::string("abcdef"),
             "flow continuation matches top");
}

} // namespace

int main() {
    test_flow_sequence();
    test_flow_map();
    test_float3_flow_sequence();
    test_flow_inside_block();
    test_nested_flow_in_map();
    test_nested_flow_in_seq();
    test_flow_map_quoted_key_with_colon();
    test_flow_map_quoted_key_value_with_colon();
    test_flow_map_plain_key_with_embedded_colon();
    test_flow_multiline_quoted_matches_top_level();
    test_flow_quoted_continuation();

    if (failures == 0) {
        std::cout << "zyaml M3 flow tests passed\n";
        return 0;
    }
    std::cerr << failures << " M3 test(s) failed\n";
    return 1;
}
