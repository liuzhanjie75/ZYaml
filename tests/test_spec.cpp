// Spec-compliance tests — edge cases from YAML 1.2 that were previously
// rejected or misparsed. Each test pins a specific spec rule.

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

const zyaml::Node* parseRoot(std::string_view yaml) {
    static thread_local zyaml::YamlDoc doc;
    auto r = zyaml::parse(yaml);
    if (!r) return nullptr;
    doc = std::move(*r);
    return &doc.root();
}

// YAML 1.2: flow collections may span multiple lines.
void test_multiline_flow_sequence() {
    constexpr std::string_view yaml =
        "items: [a,\n"
        " b,\n"
        " c]\n";
    auto root = parseRoot(yaml);
    CHECK(root != nullptr, "multiline flow seq parse");
    if (!root) return;
    const auto* items = root->find("items");
    CHECK(items != nullptr && items->isSequence(), "items is seq");
    CHECK_EQ(items->size(), 3u, "3 items");
    CHECK_EQ(std::string((*items)[0].asString()), "a", "item 0");
    CHECK_EQ(std::string((*items)[2].asString()), "c", "item 2");
}

void test_multiline_flow_map() {
    constexpr std::string_view yaml =
        "m: {a: 1,\n"
        " b: 2}\n";
    auto root = parseRoot(yaml);
    CHECK(root != nullptr, "multiline flow map parse");
    if (!root) return;
    const auto* m = root->find("m");
    CHECK(m != nullptr && m->isMap(), "m is map");
    CHECK_EQ(m->size(), 2u, "2 keys");
    CHECK_EQ(std::string(m->find("a")->asString()), "1", "m.a");
    CHECK_EQ(std::string(m->find("b")->asString()), "2", "m.b");
}

// Multi-line double-quoted scalar: bare newline folds to a space.
void test_multiline_double_quoted_fold() {
    constexpr std::string_view yaml =
        "s: \"line1\n"
        " line2\"\n";
    auto root = parseRoot(yaml);
    CHECK(root != nullptr, "multiline dq parse");
    if (!root) return;
    const auto* s = root->find("s");
    if (s) CHECK_EQ(std::string(s->asString()), "line1 line2", "folded value");
}

// Multi-line double-quoted with trailing backslash: line continuation
// folds to nothing (no space).
void test_double_quoted_continuation() {
    constexpr std::string_view yaml =
        "s: \"abc\\\n"
        "    def\"\n";
    auto root = parseRoot(yaml);
    CHECK(root != nullptr, "continuation parse");
    if (!root) return;
    const auto* s = root->find("s");
    if (s) CHECK_EQ(std::string(s->asString()), "abcdef", "continuation value");
}

// Multi-line single-quoted scalar: newline folds to a space.
void test_multiline_single_quoted_fold() {
    constexpr std::string_view yaml =
        "s: 'line1\n"
        " line2'\n";
    auto root = parseRoot(yaml);
    CHECK(root != nullptr, "multiline sq parse");
    if (!root) return;
    const auto* s = root->find("s");
    if (s) CHECK_EQ(std::string(s->asString()), "line1 line2", "sq folded value");
}

// `a:b` (colon without space) is a plain scalar, not a map entry.
void test_colon_no_space_is_scalar() {
    auto root = parseRoot("a:b\n");
    CHECK(root != nullptr, "a:b parse");
    if (!root) return;
    // The whole "a:b" is one scalar at the root (not a map).
    CHECK(root->isScalar(), "a:b is a scalar");
    if (root->isScalar()) CHECK_EQ(std::string(root->asString()), "a:b", "a:b value");
}

// A scalar value containing a colon-but-no-space stays intact.
void test_value_with_embedded_colon() {
    auto root = parseRoot("url: https://example.com\n");
    CHECK(root != nullptr, "url parse");
    if (!root) return;
    const auto* url = root->find("url");
    if (url) CHECK_EQ(std::string(url->asString()), "https://example.com", "url value");
}

// Tabs are forbidden for indentation.
void test_tab_indent_rejected() {
    auto doc = zyaml::parse("a:\n\tb: 1\n");
    CHECK(!doc.has_value(), "tab indent should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::BadIndent,
          "tab indent error code");
}

// Root-level flow collection: `[1, 2, 3]` as the document.
void test_root_flow_sequence() {
    auto root = parseRoot("[1, 2, 3]\n");
    CHECK(root != nullptr, "root flow seq parse");
    if (!root) return;
    CHECK(root->isSequence(), "root is seq");
    CHECK_EQ(root->size(), 3u, "3 elements");
    CHECK_EQ(std::string((*root)[0].asString()), "1", "elem 0");
}

void test_root_flow_map() {
    auto root = parseRoot("{a: 1, b: 2}\n");
    CHECK(root != nullptr, "root flow map parse");
    if (!root) return;
    CHECK(root->isMap(), "root is map");
    CHECK_EQ(root->size(), 2u, "2 keys");
}

// Indented "---" is a plain scalar, not a document marker.
void test_indented_doc_marker_is_scalar() {
    auto root = parseRoot("  ---\n");
    CHECK(root != nullptr, "indented --- parse");
    if (!root) return;
    CHECK(root->isScalar(), "indented --- is scalar");
    if (root->isScalar()) CHECK_EQ(std::string(root->asString()), "---", "--- value");
}

// --- must be a complete token, followed by whitespace/EOL. "---foo" is a
// plain scalar — previously the marker check matched the prefix and the
// rest of the line was silently dropped.
void test_doc_marker_prefix_is_scalar() {
    auto root = parseRoot("---foo\n");
    CHECK(root != nullptr, "---foo parse");
    if (!root) return;
    CHECK(root->isScalar(), "---foo is a plain scalar (not a doc marker)");
    if (root->isScalar()) CHECK_EQ(std::string(root->asString()), "---foo", "---foo value");
}

// ... must likewise be a complete token. "...bar" is a plain scalar.
void test_doc_end_marker_prefix_is_scalar() {
    auto root = parseRoot("...bar\n");
    CHECK(root != nullptr, "...bar parse");
    if (!root) return;
    CHECK(root->isScalar(), "...bar is a plain scalar (not a doc-end marker)");
    if (root->isScalar()) CHECK_EQ(std::string(root->asString()), "...bar", "...bar value");
}

// Mismatched close-bracket type (e.g. '[1, {a: 2]]') must be rejected
// at the outer opener, not silently accepted via depth counting.
void test_flow_bracket_type_mismatch_seq() {
    auto doc = zyaml::parse("x: [1, {a: 2]]\n");
    CHECK(!doc.has_value(), "mismatched ] closing inner { should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnclosedFlow,
          "mismatched bracket type error code");
}

void test_flow_bracket_type_mismatch_map() {
    auto doc = zyaml::parse("x: {a: [1, 2}}\n");
    CHECK(!doc.has_value(), "mismatched } closing inner [ should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnclosedFlow,
          "mismatched bracket type error code");
}

// Deeply nested (20 levels) flow collections are legal YAML and must not
// be rejected by an artificial depth cap.
void test_deep_flow_nesting_accepted() {
    std::string yaml = "s: ";
    for (int i = 0; i < 20; ++i) yaml += '[';
    yaml += "1";
    for (int i = 0; i < 20; ++i) yaml += ']';
    yaml += '\n';
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [deep] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "20-deep nested flow seq should parse");
    if (!doc) return;
    const auto* s = doc->root().find("s");
    CHECK(s != nullptr && s->isSequence(), "s is a seq");
    // Drill to the innermost scalar.
    const zyaml::Node* cur = s;
    for (int i = 0; i < 20; ++i) {
        CHECK(cur != nullptr && cur->isSequence(), "level is seq");
        if (!cur || !cur->isSequence()) return;
        cur = &(*cur)[0];
    }
    if (cur) CHECK_EQ(std::string(cur->asString()), "1", "innermost value");
}

} // namespace

int main() {
    test_multiline_flow_sequence();
    test_multiline_flow_map();
    test_multiline_double_quoted_fold();
    test_double_quoted_continuation();
    test_multiline_single_quoted_fold();
    test_colon_no_space_is_scalar();
    test_value_with_embedded_colon();
    test_tab_indent_rejected();
    test_root_flow_sequence();
    test_root_flow_map();
    test_indented_doc_marker_is_scalar();
    test_doc_marker_prefix_is_scalar();
    test_doc_end_marker_prefix_is_scalar();
    test_flow_bracket_type_mismatch_seq();
    test_flow_bracket_type_mismatch_map();
    test_deep_flow_nesting_accepted();

    if (failures == 0) {
        std::cout << "zyaml spec-compliance tests passed\n";
        return 0;
    }
    std::cerr << failures << " spec test(s) failed\n";
    return 1;
}
