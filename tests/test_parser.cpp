// M1 test — block map/scalar parsing with insertion-order preservation.
//
// Drives the public API: zyaml::parse(string_view) -> Result<YamlDoc>
// where YamlDoc exposes .root(). The root of `a: 1\nb: 2` must be a Map
// that preserves insertion order and whose scalar values convert via
// as<int>(). This test directly pins the legacy library's bug — std::map
// storage sorted keys alphabetically; the new library must NOT.

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

void test_parse_simple_block_map() {
    constexpr std::string_view yaml = "a: 1\nb: 2\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "parse should succeed");
    if (!doc) return;
    const auto& root = doc->root();
    CHECK(root.isMap(), "root should be a map");
    CHECK_EQ(root.size(), 2u, "root size");

    const auto* a = root.find("a");
    CHECK(a != nullptr, "find(\"a\") should find the key");
    CHECK(a->isScalar(), "a's value should be a scalar");
    auto ai = a->as<int>();
    CHECK(ai.has_value(), "as<int>() should succeed on \"1\"");
    CHECK_EQ(*ai, 1, "a == 1");

    const auto* b = root.find("b");
    CHECK(b != nullptr, "find(\"b\") should find the key");
    auto bi = b->as<int>();
    CHECK(bi.has_value(), "as<int>() should succeed on \"2\"");
    CHECK_EQ(*bi, 2, "b == 2");

    CHECK(root.find("nonexistent") == nullptr, "find(missing) should be nullptr");
}

void test_map_preserves_insertion_order() {
    // Legacy yaml.hpp used std::map<std::string, Node> which sorted keys
    // alphabetically — "zebra" came before "apple". This pins the new lib
    // to insertion order.
    constexpr std::string_view yaml =
        "zebra: 1\n"
        "apple: 2\n"
        "mango: 3\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "parse should succeed");
    if (!doc) return;

    std::vector<std::string> keys;
    for (const auto& item : doc->root().items()) {
        keys.push_back(std::string(item.key));
    }
    CHECK_EQ(keys.size(), 3u, "items count");
    CHECK_EQ(keys[0], "zebra", "first key is zebra (insertion order)");
    CHECK_EQ(keys[1], "apple", "second key is apple");
    CHECK_EQ(keys[2], "mango", "third key is mango");
}

void test_scalar_string_value() {
    constexpr std::string_view yaml = "name: hello\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("name");
    CHECK(n != nullptr, "find name");
    auto s = n->as<std::string>();
    CHECK(s.has_value(), "as<string>() should succeed");
    CHECK_EQ(*s, std::string("hello"), "name == hello");
}

} // namespace

int main() {
    test_parse_simple_block_map();
    test_map_preserves_insertion_order();
    test_scalar_string_value();

    if (failures == 0) {
        std::cout << "zyaml M1 parser tests passed\n";
        return 0;
    }
    std::cerr << failures << " M1 test(s) failed\n";
    return 1;
}
