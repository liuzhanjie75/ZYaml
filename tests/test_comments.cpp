// M6 test — comment preservation.
//
// Comments must survive parse and be accessible on the Node. This is the
// core differentiator vs the legacy library (which dropped all comments).
//
// Comment model: each Node has Comments { pre (lines before), inline_ (the
// # ... on the node's own line) }. M6 scope: pre + inline.

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import ZYaml;

namespace {

int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } } while(0)

#define CHECK_EQ(a, b, msg) \
    do { auto _va = (a); auto _vb = (b); if (!(_va == _vb)) { std::cerr << "FAIL: " << (msg) << "\n"; ++failures; } } while(0)

void test_inline_comment() {
    auto doc = zyaml::parse("version: 1.0   # the schema version\n");
    if (!doc) { std::cerr << "  [inline] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "inline parse should succeed");
    if (!doc) return;
    const auto* v = doc->root().find("version");
    CHECK(v != nullptr, "find version");
    CHECK_EQ(std::string(v->asString()), "1.0", "version value");
    const auto& c = v->comments();
    CHECK(c.inline_.has_value(), "inline comment present");
    if (c.inline_) CHECK_EQ(*c.inline_, std::string("# the schema version"), "inline text");
}

void test_pre_comment_on_key() {
    auto doc = zyaml::parse("# header comment\nname: hello\n");
    if (!doc) { std::cerr << "  [pre] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "pre parse should succeed");
    if (!doc) return;
    const auto* n = doc->root().find("name");
    CHECK(n != nullptr, "find name");
    const auto& c = n->comments();
    CHECK(!c.pre.empty(), "pre comment present");
    if (!c.pre.empty()) CHECK_EQ(c.pre[0], std::string("# header comment"), "pre text");
}

void test_comments_between_entries() {
    auto doc = zyaml::parse("a: 1\n# comment about b\nb: 2\n");
    if (!doc) { std::cerr << "  [between] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "between parse should succeed");
    if (!doc) return;
    const auto* a = doc->root().find("a");
    CHECK(a != nullptr, "find a");
    CHECK(a->comments().pre.empty(), "a has no pre comment");
    const auto* b = doc->root().find("b");
    CHECK(b != nullptr, "find b");
    CHECK(!b->comments().pre.empty(), "b has pre comment");
    if (!b->comments().pre.empty()) {
        CHECK_EQ(b->comments().pre[0], std::string("# comment about b"), "b pre text");
    }
}

void test_comment_preserved_through_nested() {
    constexpr std::string_view yaml =
        "# top\n"
        "environment:\n"
        "  enable_tonemap: true   # run tonemap pass\n"
        "  sky:\n"
        "    # the horizon color\n"
        "    horizon_color: [0.28, 0.38, 0.62]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [nested] error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "nested parse should succeed");
    if (!doc) return;
    const auto* env = doc->root().find("environment");
    CHECK(env != nullptr, "find environment");
    const auto* tm = env->find("enable_tonemap");
    CHECK(tm != nullptr, "find enable_tonemap");
    CHECK(tm->comments().inline_.has_value(), "enable_tonemap inline comment");
    if (tm->comments().inline_) {
        CHECK_EQ(*tm->comments().inline_, std::string("# run tonemap pass"),
                 "enable_tonemap inline text");
    }
    const auto* sky = env->find("sky");
    CHECK(sky != nullptr, "find sky");
    const auto* hc = sky->find("horizon_color");
    CHECK(hc != nullptr, "find horizon_color");
    CHECK(!hc->comments().pre.empty(), "horizon_color pre comment");
    if (!hc->comments().pre.empty()) {
        CHECK_EQ(hc->comments().pre[0], std::string("# the horizon color"),
                 "horizon_color pre text");
    }
}

} // namespace

int main() {
    test_inline_comment();
    test_pre_comment_on_key();
    test_comments_between_entries();
    test_comment_preserved_through_nested();

    if (failures == 0) {
        std::cout << "zyaml M6 comment tests passed\n";
        return 0;
    }
    std::cerr << failures << " M6 test(s) failed\n";
    return 1;
}
