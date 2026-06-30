// M3 test — flow style [a, b] and {k: v}, mixed with block.
//
// Drives: flow sequences and flow maps, including the float3 shape the
// scene files actually use (color: [0.28, 0.38, 0.62]).

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

void test_flow_sequence() {
    constexpr std::string_view yaml = "items: [apple, banana, cherry]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [flow-seq] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "flow-seq parse should succeed");
    if (!doc) return;
    const auto* items = doc->root().find("items");
    CHECK(items != nullptr, "find items");
    CHECK(items->isSequence(), "items should be a sequence");
    CHECK_EQ(items->size(), 3u, "items size");
    CHECK_EQ(std::string((*items)[0].asString()), "apple", "elem 0");
    CHECK_EQ(std::string((*items)[1].asString()), "banana", "elem 1");
    CHECK_EQ(std::string((*items)[2].asString()), "cherry", "elem 2");
}

void test_flow_map() {
    constexpr std::string_view yaml = "point: {x: 1, y: 2, z: 3}\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [flow-map] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "flow-map parse should succeed");
    if (!doc) return;
    const auto* point = doc->root().find("point");
    CHECK(point != nullptr, "find point");
    CHECK(point->isMap(), "point should be a map");
    CHECK_EQ(point->size(), 3u, "point size");
    auto x = point->find("x")->as<int>();
    CHECK(x.has_value(), "x as<int>");
    CHECK_EQ(*x, 1, "x == 1");
    auto y = point->find("y")->as<int>();
    CHECK_EQ(*y, 2, "y == 2");
    auto z = point->find("z")->as<int>();
    CHECK_EQ(*z, 3, "z == 3");
}

void test_float3_flow_sequence() {
    // The exact shape ZeroEngine's scene files use for vectors.
    constexpr std::string_view yaml = "color: [0.28, 0.38, 0.62]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [float3] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "float3 parse should succeed");
    if (!doc) return;
    const auto* color = doc->root().find("color");
    CHECK(color != nullptr, "find color");
    CHECK(color->isSequence(), "color should be a sequence");
    CHECK_EQ(color->size(), 3u, "color size");
    // M3 only has int/string conversion; float arrives in M4. Verify the
    // scalar text round-trips for now.
    CHECK_EQ(std::string((*color)[0].asString()), "0.28", "color[0] text");
    CHECK_EQ(std::string((*color)[1].asString()), "0.38", "color[1] text");
    CHECK_EQ(std::string((*color)[2].asString()), "0.62", "color[2] text");
}

void test_flow_inside_block() {
    // Block map with a flow sequence value — the real scene shape.
    constexpr std::string_view yaml =
        "transform:\n"
        "  position: [1.0, 2.0, 3.0]\n"
        "  scale: [4.0, 5.0, 6.0]\n";
    auto doc = zyaml::parse(yaml);
    if (!doc) { std::cerr << "  [flow-in-block] parse error: " << doc.error().format() << "\n"; }
    CHECK(doc.has_value(), "flow-in-block parse should succeed");
    if (!doc) return;
    const auto* tf = doc->root().find("transform");
    CHECK(tf != nullptr, "find transform");
    CHECK(tf->isMap(), "transform is map");
    const auto* pos = tf->find("position");
    CHECK(pos != nullptr, "find position");
    CHECK(pos->isSequence(), "position is seq");
    CHECK_EQ(pos->size(), 3u, "position size");
    CHECK_EQ(std::string((*pos)[0].asString()), "1.0", "pos[0]");
    const auto* scale = tf->find("scale");
    CHECK(scale != nullptr, "find scale");
    CHECK_EQ(scale->size(), 3u, "scale size");
    CHECK_EQ(std::string((*scale)[2].asString()), "6.0", "scale[2]");
}

} // namespace

int main() {
    test_flow_sequence();
    test_flow_map();
    test_float3_flow_sequence();
    test_flow_inside_block();

    if (failures == 0) {
        std::cout << "zyaml M3 flow tests passed\n";
        return 0;
    }
    std::cerr << failures << " M3 test(s) failed\n";
    return 1;
}
