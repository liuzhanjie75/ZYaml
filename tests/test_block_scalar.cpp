// M10 test — block scalars (| and >) with chomping.
//
// | literal: preserves newlines exactly.
// > folded: single newlines → spaces; blank lines → paragraph breaks.
// Chomping: |- strip (no trailing newline), |+ keep (all trailing), | clip
// (one trailing newline, the default).

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

void test_literal_block() {
    constexpr std::string_view yaml =
        "text: |\n"
        "  line one\n"
        "  line two\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [lit] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "literal block parse should succeed");
    if (!doc) return;
    const auto* t = doc->root().find("text");
    CHECK(t != nullptr, "find text");
    // Clip chomping (default): content + single trailing newline.
    if (t) CHECK_EQ(std::string(t->asString()), "line one\nline two\n", "literal value");
}

void test_literal_strip() {
    constexpr std::string_view yaml =
        "text: |-\n"
        "  line one\n"
        "  line two\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [strip] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "strip parse should succeed");
    if (!doc) return;
    const auto* t = doc->root().find("text");
    if (t) CHECK_EQ(std::string(t->asString()), "line one\nline two", "strip value (no trailing newline)");
}

void test_literal_keep() {
    constexpr std::string_view yaml =
        "text: |+\n"
        "  line one\n"
        "  line two\n\n\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [keep] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "keep parse should succeed");
    if (!doc) return;
    const auto* t = doc->root().find("text");
    if (t) CHECK_EQ(std::string(t->asString()), "line one\nline two\n\n\n", "keep value (all trailing newlines)");
}

void test_folded_block() {
    constexpr std::string_view yaml =
        "text: >\n"
        "  folded line one\n"
        "  continues here\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [fold] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "folded parse should succeed");
    if (!doc) return;
    const auto* t = doc->root().find("text");
    // Folded: single newline → space, + clip trailing newline.
    if (t) CHECK_EQ(std::string(t->asString()), "folded line one continues here\n", "folded value");
}

void test_folded_strip() {
    constexpr std::string_view yaml =
        "text: >-\n"
        "  word one\n"
        "  word two\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [fold-strip] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "fold-strip parse should succeed");
    if (!doc) return;
    const auto* t = doc->root().find("text");
    if (t) CHECK_EQ(std::string(t->asString()), "word one word two", "folded strip value");
}

void test_folded_paragraph_break() {
    // Blank line in folded → paragraph break (preserved newline).
    constexpr std::string_view yaml =
        "text: >\n"
        "  paragraph one\n"
        "\n"
        "  paragraph two\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [fold-para] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "fold-para parse should succeed");
    if (!doc) return;
    const auto* t = doc->root().find("text");
    // Fold: lines within a paragraph joined by space; blank line → \n preserved.
    if (t) CHECK_EQ(std::string(t->asString()), "paragraph one\nparagraph two\n", "folded paragraph value");
}

void test_block_scalar_after_other_keys() {
    // Block scalar as a non-first map entry.
    constexpr std::string_view yaml =
        "name: hello\n"
        "desc: |\n"
        "  multi\n"
        "  line\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [after] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "after parse should succeed");
    if (!doc) return;
    const auto* d = doc->root().find("desc");
    if (d) CHECK_EQ(std::string(d->asString()), "multi\nline\n", "desc value");
}

} // namespace

int main() {
    test_literal_block();
    test_literal_strip();
    test_literal_keep();
    test_folded_block();
    test_folded_strip();
    test_folded_paragraph_break();
    test_block_scalar_after_other_keys();

    if (failures == 0) {
        std::cout << "zyaml M10 block scalar tests passed\n";
        return 0;
    }
    std::cerr << failures << " M10 test(s) failed\n";
    return 1;
}
