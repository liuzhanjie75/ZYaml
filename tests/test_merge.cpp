// M9 test — merge keys (<<) and tags (!).
//
// Merge key `<<: *anchor` splices the referenced map's entries into the
// current map WITHOUT overwriting explicitly-set keys. Tags `!tag` are
// preserved on the node but don't change parsing (M9 scope: recognize +
// preserve, no type resolution).

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

void test_merge_key_splices_entries() {
    constexpr std::string_view yaml =
        "base: &base\n"
        "  color: red\n"
        "  size: 10\n"
        "derived:\n"
        "  <<: *base\n"
        "  size: 20\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [merge] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "merge parse should succeed");
    if (!doc) return;
    const auto* derived = doc->root().find("derived");
    CHECK(derived != nullptr, "find derived");
    CHECK(derived->isMap(), "derived is a map");
    // Merged from base:
    const auto* color = derived->find("color");
    CHECK(color != nullptr, "merged color present");
    if (color) CHECK_EQ(std::string(color->asString()), "red", "merged color == red");
    // Explicit key wins over merge:
    const auto* size = derived->find("size");
    CHECK(size != nullptr, "size present");
    if (size) {
        auto sv = size->as<int>();
        CHECK(sv.has_value() && *sv == 20, "explicit size == 20 (not merged 10)");
    }
}

void test_merge_key_with_alias_map() {
    constexpr std::string_view yaml =
        "defaults: &def\n"
        "  timeout: 30\n"
        "  retries: 3\n"
        "server:\n"
        "  <<: *def\n"
        "  host: localhost\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [merge2] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "merge2 parse should succeed");
    if (!doc) return;
    const auto* server = doc->root().find("server");
    CHECK(server != nullptr, "find server");
    CHECK_EQ(server->size(), 3u, "server has 3 entries (2 merged + 1 explicit)");
    const auto* host = server->find("host");
    CHECK(host != nullptr, "host present");
    if (host) CHECK_EQ(std::string(host->asString()), "localhost", "host == localhost");
    const auto* timeout = server->find("timeout");
    CHECK(timeout != nullptr, "merged timeout present");
    const auto* retries = server->find("retries");
    CHECK(retries != nullptr, "merged retries present");
}

void test_tag_preserved_on_scalar() {
    // A tag like !str is recognized and preserved but doesn't change how
    // the value is parsed (it's still a plain scalar).
    constexpr std::string_view yaml = "value: !str hello\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [tag] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "tag parse should succeed");
    if (!doc) return;
    const auto* v = doc->root().find("value");
    CHECK(v != nullptr, "find value");
    if (v) CHECK_EQ(std::string(v->asString()), "hello", "tagged scalar value");
}

void test_tag_on_map() {
    constexpr std::string_view yaml =
        "data: !map\n"
        "  x: 1\n"
        "  y: 2\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [tag-map] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "tag-map parse should succeed");
    if (!doc) return;
    const auto* data = doc->root().find("data");
    CHECK(data != nullptr, "find data");
    CHECK(data->isMap(), "tagged value is still a map");
    CHECK_EQ(data->size(), 2u, "data has 2 entries");
}

} // namespace

int main() {
    test_merge_key_splices_entries();
    test_merge_key_with_alias_map();
    test_tag_preserved_on_scalar();
    test_tag_on_map();

    if (failures == 0) {
        std::cout << "zyaml M9 merge/tag tests passed\n";
        return 0;
    }
    std::cerr << failures << " M9 test(s) failed\n";
    return 1;
}
