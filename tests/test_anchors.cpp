// M8 test — anchors (&name) and aliases (*name).
//
// YAML anchors let a node be referenced by name later. An alias resolves to
// the anchored node's value (a deep copy at parse time — mutation through
// an alias does NOT propagate back to the original). Unknown/duplicate
// anchors are errors.

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

void test_anchor_and_alias() {
    // A base map anchored, then aliased under another key. The alias should
    // resolve to the same content.
    constexpr std::string_view yaml =
        "base: &base\n"
        "  color: red\n"
        "  size: 10\n"
        "copy: *base\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [anchor] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "anchor parse should succeed");
    if (!doc) return;
    const auto* copy = doc->root().find("copy");
    CHECK(copy != nullptr, "find copy");
    CHECK(copy->isMap(), "copy is a map (alias resolved)");
    CHECK_EQ(copy->size(), 2u, "copy has 2 entries");
    const auto* color = copy->find("color");
    CHECK(color != nullptr, "copy.color found");
    if (color) CHECK_EQ(std::string(color->asString()), "red", "copy.color == red");
    const auto* size = copy->find("size");
    CHECK(size != nullptr, "copy.size found");
    if (size) {
        auto sv = size->as<int>();
        CHECK(sv.has_value() && *sv == 10, "copy.size == 10");
    }
}

void test_anchor_scalar_alias() {
    constexpr std::string_view yaml =
        "name: &n \"hello\"\n"
        "greeting: *n\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [scalar-anchor] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "scalar anchor parse should succeed");
    if (!doc) return;
    const auto* g = doc->root().find("greeting");
    CHECK(g != nullptr, "find greeting");
    if (g) CHECK_EQ(std::string(g->asString()), "hello", "greeting == hello");
}

void test_unknown_anchor_errors() {
    constexpr std::string_view yaml = "x: *nonexistent\n";
    auto doc = zyaml::parse(yaml);
    CHECK(!doc.has_value(), "unknown alias should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnknownAnchor,
          "error code should be UnknownAnchor");
}

void test_duplicate_anchor_errors() {
    constexpr std::string_view yaml =
        "a: &dup 1\n"
        "b: &dup 2\n";
    auto doc = zyaml::parse(yaml);
    CHECK(!doc.has_value(), "duplicate anchor should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::DuplicateAnchor,
          "error code should be DuplicateAnchor");
}

void test_alias_in_sequence() {
    constexpr std::string_view yaml =
        "first: &item \"apple\"\n"
        "list:\n"
        "  - *item\n"
        "  - banana\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [seq-alias] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "seq alias parse should succeed");
    if (!doc) return;
    const auto* list = doc->root().find("list");
    CHECK(list != nullptr && list->isSequence(), "find list seq");
    CHECK_EQ(list->size(), 2u, "list size");
    CHECK_EQ(std::string((*list)[0].asString()), "apple", "list[0] == apple");
    CHECK_EQ(std::string((*list)[1].asString()), "banana", "list[1] == banana");
}

} // namespace

int main() {
    test_anchor_and_alias();
    test_anchor_scalar_alias();
    test_unknown_anchor_errors();
    test_duplicate_anchor_errors();
    test_alias_in_sequence();

    if (failures == 0) {
        std::cout << "zyaml M8 anchor tests passed\n";
        return 0;
    }
    std::cerr << failures << " M8 test(s) failed\n";
    return 1;
}
