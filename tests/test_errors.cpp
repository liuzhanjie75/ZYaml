// M11 test — error details + multi-document streams.
//
// Error paths: verify error code + location (line/col) for each failure
// type. Multi-doc: --- separates documents in a single stream; YamlDoc
// exposes multiDocs().

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

void test_bad_indent_error() {
    auto doc = zyaml::parse("a: 1\n b: 2\n");  // inconsistent indent
    CHECK(!doc.has_value(), "bad indent should error");
    if (!doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::BadIndent,
          "error code should be BadIndent");
    CHECK(doc.error().where.line > 0, "error has a line number");
}

void test_unclosed_quote_error() {
    auto doc = zyaml::parse("s: \"unterminated\n");
    CHECK(!doc.has_value(), "unclosed quote should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnclosedQuote,
          "error code should be UnclosedQuote");
}

void test_unclosed_flow_error() {
    auto doc = zyaml::parse("x: [1, 2\n");
    CHECK(!doc.has_value(), "unclosed flow should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnclosedFlow,
          "error code should be UnclosedFlow");
}

void test_bad_escape_error() {
    auto doc = zyaml::parse("s: \"bad \\q\"\n");
    CHECK(!doc.has_value(), "bad escape should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::BadEscape,
          "error code should be BadEscape");
}

void test_unknown_anchor_error() {
    auto doc = zyaml::parse("x: *missing\n");
    CHECK(!doc.has_value(), "unknown anchor should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnknownAnchor,
          "error code should be UnknownAnchor");
}

void test_duplicate_anchor_error() {
    auto doc = zyaml::parse("a: &x 1\nb: &x 2\n");
    CHECK(!doc.has_value(), "duplicate anchor should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::DuplicateAnchor,
          "error code should be DuplicateAnchor");
}

void test_mixed_map_seq_error() {
    auto doc = zyaml::parse("a: 1\n- lost\n");
    CHECK(!doc.has_value(), "mixed map/seq should error");
    if (doc.has_value()) return;
    CHECK(doc.error().code == zyaml::YamlErrorCode::UnexpectedToken,
          "error code should be UnexpectedToken");
}

void test_empty_input_is_null() {
    auto doc = zyaml::parse("");
    CHECK(doc.has_value(), "empty input should succeed (null root)");
    if (!doc) return;
    CHECK(doc->root().isNull(), "empty input root is null");
}

void test_whitespace_only_is_null() {
    auto doc = zyaml::parse("   \n  \n");
    CHECK(doc.has_value(), "whitespace-only should succeed");
    if (!doc) return;
    CHECK(doc->root().isNull(), "whitespace-only root is null");
}

void test_single_doc_marker() {
    // A leading --- is the document start marker — should be optional.
    auto doc = zyaml::parse("---\na: 1\n");
    CHECK(doc.has_value(), "doc-start marker parse should succeed");
    if (!doc) return;
    const auto* a = doc->root().find("a");
    CHECK(a != nullptr, "find a");
    if (a) CHECK_EQ(std::string(a->asString()), "1", "a == 1");
}

void test_doc_end_marker() {
    // ... ends a document.
    auto doc = zyaml::parse("a: 1\n...\n");
    CHECK(doc.has_value(), "doc-end marker parse should succeed");
    if (!doc) return;
    const auto* a = doc->root().find("a");
    CHECK(a != nullptr, "find a");
    if (a) CHECK_EQ(std::string(a->asString()), "1", "a == 1");
}

void test_multi_document() {
    constexpr std::string_view yaml =
        "a: 1\n"
        "---\n"
        "b: 2\n"
        "---\n"
        "c: 3\n";
    auto docs = zyaml::parseMultiDoc(yaml);
    CHECK(docs.has_value(), "multi-doc parse should succeed");
    if (!docs) return;
    CHECK_EQ(docs->size(), 3u, "3 documents");
    if (docs->size() < 3) return;
    // Each doc should have one key.
    const auto& d0 = (*docs)[0].root();
    const auto& d1 = (*docs)[1].root();
    const auto& d2 = (*docs)[2].root();
    CHECK(d0.find("a") != nullptr, "doc 0 has 'a'");
    CHECK(d1.find("b") != nullptr, "doc 1 has 'b'");
    CHECK(d2.find("c") != nullptr, "doc 2 has 'c'");
}

void test_multi_doc_with_end_markers() {
    constexpr std::string_view yaml =
        "a: 1\n"
        "...\n"
        "---\n"
        "b: 2\n"
        "...\n";
    auto docs = zyaml::parseMultiDoc(yaml);
    CHECK(docs.has_value(), "multi-doc with end markers parse should succeed");
    if (!docs) return;
    CHECK_EQ(docs->size(), 2u, "2 documents");
}

} // namespace

int main() {
    test_bad_indent_error();
    test_unclosed_quote_error();
    test_unclosed_flow_error();
    test_bad_escape_error();
    test_unknown_anchor_error();
    test_duplicate_anchor_error();
    test_mixed_map_seq_error();
    test_empty_input_is_null();
    test_whitespace_only_is_null();
    test_single_doc_marker();
    test_doc_end_marker();
    test_multi_document();
    test_multi_doc_with_end_markers();

    if (failures == 0) {
        std::cout << "zyaml M11 error/multidoc tests passed\n";
        return 0;
    }
    std::cerr << failures << " M11 test(s) failed\n";
    return 1;
}
