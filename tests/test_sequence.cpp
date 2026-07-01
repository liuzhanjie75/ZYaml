// M2 test — block sequences, nesting, and ordered iteration.

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

void test_block_sequence_of_scalars() {
    constexpr std::string_view yaml =
        "items:\n"
        "  - apple\n"
        "  - banana\n"
        "  - cherry\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [seq-of-scalars] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "seq-of-scalars parse should succeed");
    if (!doc) return;
    const auto* items = doc->root().find("items");
    CHECK(items != nullptr, "find items");
    CHECK(items->isSequence(), "items should be a sequence");
    CHECK_EQ(items->size(), 3u, "items size");

    std::vector<std::string> got;
    for (const auto& v : items->elements()) {
        got.push_back(std::string(v.asString()));
    }
    CHECK_EQ(got.size(), 3u, "elements count");
    CHECK_EQ(got[0], "apple", "first element");
    CHECK_EQ(got[1], "banana", "second element");
    CHECK_EQ(got[2], "cherry", "third element");
}

void test_nested_map_in_map() {
    constexpr std::string_view yaml =
        "outer:\n"
        "  inner_key: inner_value\n"
        "  number: 42\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [nested-map] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "nested-map parse should succeed");
    if (!doc) return;
    const auto* outer = doc->root().find("outer");
    CHECK(outer != nullptr, "find outer");
    CHECK(outer->isMap(), "outer should be a map");

    const auto* inner = outer->find("inner_key");
    CHECK(inner != nullptr, "find outer.inner_key");
    CHECK_EQ(std::string(inner->asString()), "inner_value", "inner value");

    const auto* num = outer->find("number");
    CHECK(num != nullptr, "find outer.number");
    auto n = num->as<int>();
    CHECK(n.has_value(), "as<int>");
    CHECK_EQ(*n, 42, "number == 42");
}

void test_sequence_of_maps() {
    constexpr std::string_view yaml =
        "nodes:\n"
        "  - name: floor\n"
        "    path: Assets/floor.obj\n"
        "  - name: helmet\n"
        "    path: Assets/helmet.obj\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [seq-of-maps] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "seq-of-maps parse should succeed");
    if (!doc) return;
    const auto* nodes = doc->root().find("nodes");
    CHECK(nodes != nullptr, "find nodes");
    CHECK(nodes->isSequence(), "nodes should be a sequence");
    CHECK_EQ(nodes->size(), 2u, "nodes size");

    const auto& n0 = (*nodes)[0];
    const auto* n0name = n0.find("name");
    CHECK(n0name != nullptr, "node 0 name");
    if (n0name) CHECK_EQ(std::string(n0name->asString()), "floor", "node 0 name");
    const auto* n0path = n0.find("path");
    CHECK(n0path != nullptr, "node 0 path");
    if (n0path) CHECK_EQ(std::string(n0path->asString()), "Assets/floor.obj", "node 0 path");

    const auto& n1 = (*nodes)[1];
    const auto* n1name = n1.find("name");
    CHECK(n1name != nullptr, "node 1 name");
    if (n1name) CHECK_EQ(std::string(n1name->asString()), "helmet", "node 1 name");
    const auto* n1path = n1.find("path");
    CHECK(n1path != nullptr, "node 1 path");
    if (n1path) CHECK_EQ(std::string(n1path->asString()), "Assets/helmet.obj", "node 1 path");
}

void test_nested_seq_with_flow_value() {
    // "- - [1, 2]" — the inner flow collection must parse as a sequence,
    // not a plain scalar string "[1, 2]". Previously the nested-seq path
    // called readScalarValue() which didn't recognize flow collections.
    constexpr std::string_view yaml = "- - [1, 2]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [nested-flow] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "nested seq with flow value parse should succeed");
    if (!doc) return;
    const auto& r = doc->root();
    CHECK(r.isSequence(), "root is seq");
    CHECK_EQ(r.size(), 1u, "1 outer element");
    CHECK(r[0].isSequence(), "elem 0 is nested seq");
    CHECK_EQ(r[0].size(), 1u, "nested seq has 1 element");
    const auto& inner = r[0][0];
    CHECK(inner.isSequence(), "inner is flow seq");
    CHECK_EQ(inner.size(), 2u, "flow seq has 2");
    CHECK_EQ(std::string(inner[0].asString()), "1", "inner[0]");
    CHECK_EQ(std::string(inner[1].asString()), "2", "inner[1]");
}

void test_nested_seq_with_flow_map() {
    constexpr std::string_view yaml = "- - {k: v}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [nested-flow-map] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "nested seq with flow map parse should succeed");
    if (!doc) return;
    const auto& root = doc->root();
    const auto& inner = root[0][0];
    CHECK(inner.isMap(), "inner is flow map");
    CHECK_EQ(std::string(inner.find("k")->asString()), "v", "inner.k");
}

} // namespace

int main() {
    test_block_sequence_of_scalars();
    test_nested_map_in_map();
    test_sequence_of_maps();
    test_nested_seq_with_flow_value();
    test_nested_seq_with_flow_map();

    if (failures == 0) {
        std::cout << "zyaml M2 sequence/nesting tests passed\n";
        return 0;
    }
    std::cerr << failures << " M2 test(s) failed\n";
    return 1;
}
