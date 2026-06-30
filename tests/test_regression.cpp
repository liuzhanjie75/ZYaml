// Regression tests for the data-integrity bugs caught in code review.
//
// Each test pins a malformed-YAML case that previously parsed successfully
// (silently dropping or accepting bad data). They must now fail with a
// specific YamlErrorCode.

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

#define CHECK_ERROR_CODE(r, code, msg) \
    do { if (!((r).error().code == (code))) { std::cerr << "FAIL: " << (msg) << " (got code " << static_cast<int>((r).error().code) << ")\n"; ++failures; } } while(0)

// P1.1: a same-indent map→seq switch must not silently drop the seq.
void test_mixed_map_seq_same_indent_rejected() {
    auto doc = zyaml::parse("a: 1\n- lost\n");
    CHECK(!doc.has_value(), "mixed map/seq at same indent should error");
    if (doc.has_value()) return;
    CHECK_ERROR_CODE(doc, zyaml::YamlErrorCode::UnexpectedToken,
                     "mixed map/seq error code");
}

// P1.2: an unclosed flow collection must error, not return empty/partial.
void test_unclosed_flow_sequence_rejected() {
    auto doc = zyaml::parse("items: [apple, banana\n");
    CHECK(!doc.has_value(), "unclosed flow seq should error");
    if (doc.has_value()) return;
    CHECK_ERROR_CODE(doc, zyaml::YamlErrorCode::UnclosedFlow,
                     "unclosed flow seq error code");
}

void test_unclosed_flow_map_rejected() {
    auto doc = zyaml::parse("point: {x: 1, y: 2\n");
    CHECK(!doc.has_value(), "unclosed flow map should error");
    if (doc.has_value()) return;
    CHECK_ERROR_CODE(doc, zyaml::YamlErrorCode::UnclosedFlow,
                     "unclosed flow map error code");
}

// P1.3: an unclosed quoted scalar must error, not return partial text.
void test_unclosed_double_quote_rejected() {
    auto doc = zyaml::parse("name: \"no end quote\n");
    CHECK(!doc.has_value(), "unclosed double quote should error");
    if (doc.has_value()) return;
    CHECK_ERROR_CODE(doc, zyaml::YamlErrorCode::UnclosedQuote,
                     "unclosed double quote error code");
}

void test_unclosed_single_quote_rejected() {
    auto doc = zyaml::parse("name: 'no end quote\n");
    CHECK(!doc.has_value(), "unclosed single quote should error");
    if (doc.has_value()) return;
    CHECK_ERROR_CODE(doc, zyaml::YamlErrorCode::UnclosedQuote,
                     "unclosed single quote error code");
}

void test_bad_escape_rejected() {
    auto doc = zyaml::parse("s: \"bad \\q escape\"\n");
    CHECK(!doc.has_value(), "unknown escape \\q should error");
    if (doc.has_value()) return;
    CHECK_ERROR_CODE(doc, zyaml::YamlErrorCode::BadEscape,
                     "bad escape error code");
}

// P2: Node copy is deleted; clone() produces an independent deep copy.
// Mutating the clone must not affect the original.
void test_node_clone_is_independent() {
    auto doc = zyaml::parse("a: 1\nb: 2\n");
    CHECK(doc.has_value(), "parse should succeed");
    if (!doc) return;
    CHECK_EQ(doc->root().size(), 2u, "original has 2 entries");

    auto copy = doc->root().clone();
    copy.appendMapEntry("c", zyaml::Node::makeScalar("3"));
    CHECK_EQ(copy.size(), 3u, "clone has 3 entries after append");
    // Original unaffected.
    CHECK_EQ(doc->root().size(), 2u, "original still has 2 entries");
    CHECK(doc->root().find("c") == nullptr, "original has no c key");
}

} // namespace

int main() {
    test_mixed_map_seq_same_indent_rejected();
    test_unclosed_flow_sequence_rejected();
    test_unclosed_flow_map_rejected();
    test_unclosed_double_quote_rejected();
    test_unclosed_single_quote_rejected();
    test_bad_escape_rejected();
    test_node_clone_is_independent();

    if (failures == 0) {
        std::cout << "zyaml regression tests passed\n";
        return 0;
    }
    std::cerr << failures << " regression test(s) failed\n";
    return 1;
}
