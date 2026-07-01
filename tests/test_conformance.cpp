// M12 — conformance: edge cases across all supported YAML features.
//
// Not the official yaml-test-suite (that would require a submodule +
// a driver). This is a curated set of edge cases that exercise the
// combinations and boundaries of what M0-M11 implemented. Each test
// pins a specific spec behavior that a regression would break.

#include <iostream>
#include <string>
#include <string_view>

import ZYaml;

namespace {

int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } } while(0)

#define CHECK_EQ(a, b, msg) \
    do { auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } } while(0)

zyaml::Node* parseRootMut(std::string_view yaml) {
    static thread_local zyaml::YamlDoc doc;
    auto r = zyaml::parse(yaml);
    if (!r) return nullptr;
    doc = std::move(*r);
    return &doc.root();
}

// For read-only tests:
const zyaml::Node* parseRoot(std::string_view yaml) {
    return parseRootMut(yaml);
}

// ── Ordering & structure ──────────────────────────────────────

void test_deeply_nested() {
    auto root = parseRoot(
        "a:\n"
        "  b:\n"
        "    c:\n"
        "      d: deep\n");
    CHECK(root != nullptr, "deep nest parse");
    if (!root) return;
    const auto* d = root->find("a")->find("b")->find("c")->find("d");
    CHECK(d != nullptr, "find a.b.c.d");
    if (d) CHECK_EQ(std::string(d->asString()), "deep", "deep value");
}

void test_map_with_sequence_value() {
    auto root = parseRoot(
        "list:\n"
        "  - one\n"
        "  - two\n"
        "  - three\n"
        "key: val\n");
    CHECK(root != nullptr, "map+seq parse");
    if (!root) return;
    const auto* list = root->find("list");
    CHECK(list != nullptr && list->isSequence(), "list is seq");
    CHECK_EQ(list->size(), 3u, "list 3 items");
    const auto* key = root->find("key");
    CHECK(key != nullptr, "key after seq");
    if (key) CHECK_EQ(std::string(key->asString()), "val", "key val");
}

void test_sequence_of_sequences() {
    auto root = parseRoot(
        "matrix:\n"
        "  - - a\n"
        "    - b\n"
        "  - - c\n"
        "    - d\n");
    CHECK(root != nullptr, "seq of seq parse");
    if (!root) return;
    const auto* m = root->find("matrix");
    CHECK(m != nullptr && m->isSequence(), "matrix is seq");
    CHECK_EQ(m->size(), 2u, "2 rows");
    CHECK((*m)[0].isSequence(), "row 0 is seq");
    CHECK_EQ((*m)[0].size(), 2u, "row 0 has 2");
    CHECK_EQ(std::string((*m)[0][0].asString()), "a", "m[0][0]");
    CHECK_EQ(std::string((*m)[1][1].asString()), "d", "m[1][1]");
}

// ── Scalar edge cases ──────────────────────────────────────────

void test_empty_string_value() {
    auto root = parseRoot("key: \"\"\n");
    CHECK(root != nullptr, "empty string parse");
    if (!root) return;
    const auto* k = root->find("key");
    CHECK(k != nullptr, "find key");
    if (k) CHECK_EQ(std::string(k->asString()), "", "empty string value");
}

void test_scalar_with_spaces() {
    auto root = parseRoot("greeting: \"hello world  \"\n");
    CHECK(root != nullptr, "spaces parse");
    if (!root) return;
    const auto* g = root->find("greeting");
    if (g) CHECK_EQ(std::string(g->asString()), "hello world  ", "trailing spaces preserved in quoted");
}

void test_int_edge_cases() {
    auto root = parseRoot("zero: 0\nneg: -42\nbig: 1000000\n");
    CHECK(root != nullptr, "int edge parse");
    if (!root) return;
    CHECK(root->find("zero")->as<int>().has_value() && *root->find("zero")->as<int>() == 0, "zero");
    CHECK(root->find("neg")->as<int>().has_value() && *root->find("neg")->as<int>() == -42, "neg");
    CHECK(root->find("big")->as<int>().has_value() && *root->find("big")->as<int>() == 1000000, "big");
}

void test_float_edge_cases() {
    auto root = parseRoot("pi: 3.14159\ne: 2.0\nzero: 0.0\nneg: -0.5\n");
    CHECK(root != nullptr, "float edge parse");
    if (!root) return;
    CHECK(root->find("pi")->as<float>().has_value(), "pi as float");
    CHECK(root->find("zero")->as<float>().has_value() && *root->find("zero")->as<float>() == 0.0f, "zero float");
    CHECK(root->find("neg")->as<float>().has_value() && *root->find("neg")->as<float>() == -0.5f, "neg float");
}

void test_bool_all_spellings() {
    auto root = parseRoot(
        "t1: true\nt2: True\nt3: TRUE\nt4: yes\nt5: on\nt6: y\n"
        "f1: false\nf2: False\nf3: no\nf4: off\nf5: n\n");
    CHECK(root != nullptr, "bool parse");
    if (!root) return;
    for (auto key : {"t1","t2","t3","t4","t5","t6"}) {
        auto v = root->find(key)->as<bool>();
        CHECK(v.has_value() && *v == true, std::string("bool true: ") + key);
    }
    for (auto key : {"f1","f2","f3","f4","f5"}) {
        auto v = root->find(key)->as<bool>();
        CHECK(v.has_value() && *v == false, std::string("bool false: ") + key);
    }
}

void test_null_variations() {
    auto root = parseRoot("a: null\nb: ~\nc: Null\nd: NULL\ne:\n");
    CHECK(root != nullptr, "null parse");
    if (!root) return;
    for (auto key : {"a","b","c","d","e"}) {
        CHECK(root->find(key)->isNull(), std::string("null: ") + key);
    }
}

// ── Flow edge cases ────────────────────────────────────────────

void test_empty_flow_seq() {
    auto root = parseRoot("empty: []\n");
    CHECK(root != nullptr, "empty flow seq parse");
    if (!root) return;
    const auto* e = root->find("empty");
    CHECK(e != nullptr && e->isSequence(), "empty is seq");
    CHECK_EQ(e->size(), 0u, "empty seq size 0");
}

void test_empty_flow_map() {
    auto root = parseRoot("config: {}\n");
    CHECK(root != nullptr, "empty flow map parse");
    if (!root) return;
    const auto* c = root->find("config");
    CHECK(c != nullptr && c->isMap(), "config is map");
    CHECK_EQ(c->size(), 0u, "empty map size 0");
}

void test_flow_seq_of_quoted() {
    auto root = parseRoot("list: [\"a\", 'b', c]\n");
    CHECK(root != nullptr, "flow quoted parse");
    if (!root) return;
    const auto* l = root->find("list");
    CHECK(l != nullptr && l->isSequence(), "list is seq");
    CHECK_EQ(std::string((*l)[0].asString()), "a", "elem 0");
    CHECK_EQ(std::string((*l)[1].asString()), "b", "elem 1");
    CHECK_EQ(std::string((*l)[2].asString()), "c", "elem 2");
}

void test_nested_flow() {
    auto root = parseRoot("x: [[1, 2], [3, 4]]\n");
    CHECK(root != nullptr, "nested flow parse");
    if (!root) return;
    const auto* x = root->find("x");
    CHECK(x != nullptr && x->isSequence(), "x is seq");
    CHECK((*x)[0].isSequence(), "x[0] is seq");
    CHECK_EQ(std::string((*x)[0][0].asString()), "1", "x[0][0]");
    CHECK_EQ(std::string((*x)[1][1].asString()), "4", "x[1][1]");
}

// ── Comment + anchor + merge combo ────────────────────────────

void test_comment_anchor_merge() {
    auto root = parseRoot(
        "# base template\n"
        "base: &b\n"
        "  # shared key\n"
        "  shared: yes\n"
        "  val: 1\n"
        "derived:\n"
        "  <<: *b   # merge in base\n"
        "  val: 2   # override\n");
    CHECK(root != nullptr, "combo parse");
    if (!root) return;
    const auto* d = root->find("derived");
    CHECK(d != nullptr, "find derived");
    CHECK(d->find("shared") != nullptr, "merged shared present");
    auto v = d->find("val")->as<int>();
    CHECK(v.has_value() && *v == 2, "explicit val wins");
    // Check comments survived.
    CHECK(d->find("val")->comments().inline_.has_value(), "val has inline comment");
}

// ── Round-trip with emit ──────────────────────────────────────

void test_round_trip_complex() {
    constexpr std::string_view yaml =
        "# scene file\n"
        "camera:\n"
        "  fov: 60\n"
        "  near: 0.1\n"
        "lights:\n"
        "  - type: directional   # sun\n"
        "    intensity: 3.0\n"
        "  - type: ambient\n"
        "    intensity: 0.1\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "complex parse");
    if (!doc) return;
    // Verify original parse has the right structure.
    const auto* cam = doc->root().find("camera");
    CHECK(cam != nullptr, "camera present");
    CHECK(cam->find("fov")->as<int>().has_value(), "camera.fov");
    const auto* lights = doc->root().find("lights");
    CHECK(lights != nullptr && lights->isSequence(), "lights seq");
    CHECK_EQ(lights->size(), 2u, "2 lights");
    CHECK(std::string((*lights)[0].find("type")->asString()) == "directional", "light 0 type");
    // Emit + re-parse: round-trip must preserve structure.
    std::string out = zyaml::emit(doc->root());
    auto re = zyaml::parse(out);
    CHECK(re.has_value(), "round-trip emit→parse must succeed");
    if (!re) return;
    CHECK(re->root().find("camera") != nullptr, "rt: camera");
    CHECK(re->root().find("lights") != nullptr, "rt: lights");
    CHECK_EQ(re->root().find("lights")->size(), 2u, "rt: 2 lights preserved");
    CHECK(std::string((*re->root().find("lights"))[0].find("type")->asString()) == "directional",
          "rt: light 0 type preserved");
}

void test_node_remove() {
    auto doc = zyaml::parse("a: 1\nb: 2\nc: 3\n");
    CHECK(doc.has_value(), "remove parse");
    if (!doc) return;
    auto& root = doc->root();
    CHECK_EQ(root.size(), 3u, "3 entries before");
    CHECK(root.remove("b"), "remove b");
    CHECK_EQ(root.size(), 2u, "2 entries after");
    CHECK(root.find("b") == nullptr, "b gone");
    CHECK(root.find("a") != nullptr, "a still there");
    CHECK(root.find("c") != nullptr, "c still there");
    CHECK(!root.remove("nonexistent"), "remove missing returns false");
}

void test_node_remove_at() {
    auto doc = zyaml::parse("list:\n  - x\n  - y\n  - z\n");
    CHECK(doc.has_value(), "removeAt parse");
    if (!doc) return;
    auto* list = doc->root().find("list");
    CHECK(list != nullptr, "find list");
    CHECK_EQ(list->size(), 3u, "3 elements");
    CHECK(list->removeAt(1), "removeAt(1)");
    CHECK_EQ(list->size(), 2u, "2 elements after");
    CHECK_EQ(std::string((*list)[0].asString()), "x", "elem 0 still x");
    CHECK_EQ(std::string((*list)[1].asString()), "z", "elem 1 is now z");
    CHECK(!list->removeAt(99), "removeAt OOB returns false");
}

void test_block_scalar_in_sequence() {
    auto root = parseRoot(
        "items:\n"
        "  - |\n"
        "    line one\n"
        "  - plain\n");
    CHECK(root != nullptr, "block scalar in seq parse");
    if (!root) return;
    const auto* items = root->find("items");
    CHECK(items != nullptr && items->isSequence(), "items is seq");
    CHECK_EQ(items->size(), 2u, "2 items");
    if (items->size() == 2) {
        CHECK_EQ(std::string((*items)[0].asString()), "line one\n", "elem 0 block scalar");
        CHECK_EQ(std::string((*items)[1].asString()), "plain", "elem 1 plain");
    }
}

// ── Block scalar round-trip ───────────────────────────────────

void test_block_scalar_round_trip() {
    constexpr std::string_view yaml =
        "shader: |\n"
        "  void main() {\n"
        "    gl_Position = vec4(1.0);\n"
        "  }\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "block scalar rt parse");
    if (!doc) return;
    const auto* s = doc->root().find("shader");
    CHECK(s != nullptr, "find shader");
    if (s) {
        CHECK_EQ(std::string(s->asString()),
                 "void main() {\n  gl_Position = vec4(1.0);\n}\n",
                 "block scalar content");
    }
    // Emit + re-parse.
    std::string out = zyaml::emit(doc->root());
    auto re = zyaml::parse(out);
    CHECK(re.has_value(), "block scalar rt re-parse");
}

} // namespace

int main() {
    test_deeply_nested();
    test_map_with_sequence_value();
    test_sequence_of_sequences();
    test_empty_string_value();
    test_scalar_with_spaces();
    test_int_edge_cases();
    test_float_edge_cases();
    test_bool_all_spellings();
    test_null_variations();
    test_empty_flow_seq();
    test_empty_flow_map();
    test_flow_seq_of_quoted();
    test_nested_flow();
    test_comment_anchor_merge();
    test_round_trip_complex();
    test_block_scalar_round_trip();
    test_node_remove();
    test_node_remove_at();
    test_block_scalar_in_sequence();

    if (failures == 0) {
        std::cout << "zyaml M12 conformance tests passed\n";
        return 0;
    }
    std::cerr << failures << " M12 test(s) failed\n";
    return 1;
}
