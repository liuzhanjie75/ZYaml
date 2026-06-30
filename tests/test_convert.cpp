// M4 test — scalar type conversions: bool, float, double, null.
//
// Pins the legacy library's bool bug: yaml.hpp's parseBool accepted only
// true/yes/on/y, silently returning false for "false"/"no"/"off". YAML 1.2
// core schema accepts all of true/false/yes/no/on/off/y/n (case-insensitive).
// This test requires all spellings to round-trip correctly.

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

// Helper: parse "k: <v>" and return the scalar's as<T>() result.
template <class T>
auto scalarAs(std::string_view v) {
    std::string yaml = "k: ";
    yaml.append(v);
    auto doc = zyaml::parse(yaml);
    if (!doc) return zyaml::Result<T>(zyaml::YamlError{});
    const auto* n = doc->root().find("k");
    if (!n) return zyaml::Result<T>(zyaml::YamlError{});
    return n->as<T>();
}

void test_bool_true_spellings() {
    for (auto s : {"true", "True", "TRUE", "yes", "Yes", "on", "On", "y", "Y"}) {
        auto r = scalarAs<bool>(s);
        CHECK(r.has_value(), std::string("bool true: ") + s);
        if (r) CHECK_EQ(*r, true, std::string("bool true value: ") + s);
    }
}

void test_bool_false_spellings() {
    // The legacy bug: "false"/"no"/"off" returned false silently — same as
    // the true spellings' absence. Here false must round-trip as bool false,
    // not be confused with "absent".
    for (auto s : {"false", "False", "FALSE", "no", "No", "off", "Off", "n", "N"}) {
        auto r = scalarAs<bool>(s);
        CHECK(r.has_value(), std::string("bool false: ") + s);
        if (r) CHECK_EQ(*r, false, std::string("bool false value: ") + s);
    }
}

void test_bool_invalid_returns_error() {
    // A non-bool string must NOT silently coerce to false — it should error.
    auto r = scalarAs<bool>("maybe");
    CHECK(!r.has_value(), "bool invalid 'maybe' should error");
    auto r2 = scalarAs<bool>("42");
    CHECK(!r2.has_value(), "bool invalid '42' should error (not a bool)");
}

void test_float_and_double() {
    auto f = scalarAs<float>("3.14");
    CHECK(f.has_value(), "float 3.14");
    if (f) CHECK_EQ(*f, 3.14f, "float value");

    auto d = scalarAs<double>("2.71828");
    CHECK(d.has_value(), "double");
    if (d) {
        double got = *d;
        double want = 2.71828;
        CHECK(got > want - 0.00001 && got < want + 0.00001, "double value approx");
    }

    // Negative + scientific.
    auto neg = scalarAs<float>("-1.5");
    CHECK(neg.has_value() && *neg == -1.5f, "float -1.5");
    auto sci = scalarAs<float>("1e3");
    CHECK(sci.has_value() && *sci == 1000.0f, "float 1e3");
}

void test_int_formats() {
    auto i = scalarAs<int>("42");
    CHECK(i.has_value() && *i == 42, "int 42");
    auto neg = scalarAs<int>("-7");
    CHECK(neg.has_value() && *neg == -7, "int -7");
    auto plus = scalarAs<int>("+7");
    CHECK(plus.has_value() && *plus == 7, "int +7");
    auto bad = scalarAs<int>("42x");
    CHECK(!bad.has_value(), "int '42x' should error");
}

void test_null_detection() {
    // null/~ / empty values.
    constexpr std::string_view yaml = "a: null\nb: ~\nc:\nd: Null\n";
    auto doc = zyaml::parse(yaml);
    CHECK(doc.has_value(), "null parse should succeed");
    if (!doc) return;
    const auto& root = doc->root();
    CHECK(root.find("a")->isNull(), "null keyword -> null node");
    CHECK(root.find("b")->isNull(), "~ -> null node");
    CHECK(root.find("c")->isNull(), "empty value -> null node");
    CHECK(root.find("d")->isNull(), "Null -> null node");
}

} // namespace

int main() {
    test_bool_true_spellings();
    test_bool_false_spellings();
    test_bool_invalid_returns_error();
    test_float_and_double();
    test_int_formats();
    test_null_detection();

    if (failures == 0) {
        std::cout << "zyaml M4 conversion tests passed\n";
        return 0;
    }
    std::cerr << failures << " M4 test(s) failed\n";
    return 1;
}
