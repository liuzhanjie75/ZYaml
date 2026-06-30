// M5 test — quoted scalars and escape sequences.
//
// Drives: single-quoted ('...' with '' escape), double-quoted ("..." with
// \n\t\"\\ escapes), and plain-scalar boundaries (a plain scalar containing
// ':' or '#' must be quoted). Also the config.yaml shape `version: "1.0"`.

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

void test_double_quoted_scalar() {
    constexpr std::string_view yaml = "name: \"hello world\"\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [dq] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "dq parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("name");
    CHECK(n != nullptr, "find name");
    CHECK_EQ(std::string(n->asString()), "hello world", "dq value");
}

void test_double_quoted_escapes() {
    // \n -> newline, \t -> tab, \\ -> backslash, \" -> quote.
    constexpr std::string_view yaml = "s: \"line1\\nline2\\t\\\"q\\\"\"\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [dq-esc] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "dq-esc parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("s");
    CHECK(n != nullptr, "find s");
    std::string want = "line1\nline2\t\"q\"";
    CHECK_EQ(std::string(n->asString()), want, "dq escape value");
}

void test_single_quoted_scalar() {
    constexpr std::string_view yaml = "name: 'hello world'\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [sq] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "sq parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("name");
    CHECK(n != nullptr, "find name");
    CHECK_EQ(std::string(n->asString()), "hello world", "sq value");
}

void test_single_quoted_escape() {
    // '' inside single quotes is a literal single quote.
    constexpr std::string_view yaml = "s: 'it''s here'\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [sq-esc] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "sq-esc parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("s");
    CHECK(n != nullptr, "find s");
    CHECK_EQ(std::string(n->asString()), "it's here", "sq escape value");
}

void test_quoted_value_with_colon() {
    // A plain scalar can't contain ':' (scanner stops at it). Quoting
    // allows it — this is how URLs and timestamps appear in YAML.
    constexpr std::string_view yaml = "url: \"https://example.com\"\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [colon] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "colon parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("url");
    CHECK(n != nullptr, "find url");
    CHECK_EQ(std::string(n->asString()), "https://example.com", "url value");
}

void test_version_quoted_string() {
    // The config.yaml shape.
    constexpr std::string_view yaml = "version: \"1.0\"\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "version parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("version");
    CHECK(n != nullptr, "find version");
    CHECK_EQ(std::string(n->asString()), "1.0", "version value");
    // It's a string, not a float — but as<float> would succeed too (1.0).
    // Verify asString round-trips the unquoted text.
}

void test_quoted_scalar_in_sequence() {
    constexpr std::string_view yaml =
        "items:\n"
        "  - \"with space\"\n"
        "  - plain\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [seq] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "seq parse should succeed");
    if (!doc) return;
    const auto* items = doc->root().find("items");
    CHECK(items != nullptr && items->isSequence(), "find items seq");
    CHECK_EQ(std::string((*items)[0].asString()), "with space", "elem 0");
    CHECK_EQ(std::string((*items)[1].asString()), "plain", "elem 1");
}

} // namespace

int main() {
    test_double_quoted_scalar();
    test_double_quoted_escapes();
    test_single_quoted_scalar();
    test_single_quoted_escape();
    test_quoted_value_with_colon();
    test_version_quoted_string();
    test_quoted_scalar_in_sequence();

    if (failures == 0) {
        std::cout << "zyaml M5 quoted scalar tests passed\n";
        return 0;
    }
    std::cerr << failures << " M5 test(s) failed\n";
    return 1;
}
