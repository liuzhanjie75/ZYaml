// ZYaml benchmark — parse/emit throughput on representative shapes.
//
// Prints timings for: small config, large flat map (the O(n^2) regression
// target), deep nesting, flow-heavy, and a full round-trip. Asserts
// generous upper bounds to catch performance regressions in CI. Bounds
// are ~10x the measured Release numbers to avoid flaky failures.
//
// Build: cmake --build build --config Release (or -DCMAKE_BUILD_TYPE=Release)
// Run:   ./bench/zyaml_bench

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

import ZYaml;

namespace {

using clk = std::chrono::high_resolution_clock;

template <class Fn>
auto ms(Fn&& fn) {
    auto t0 = clk::now();
    fn();
    auto t1 = clk::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void report(const char* label, double parse_ms, std::size_t bytes,
            double bound_ms) {
    auto mbs = bytes / (parse_ms / 1000.0) / (1024.0 * 1024.0);
    std::printf("  %-28s parse %7.2f ms  %7.2f MB/s  (bound %.0f ms)\n",
                label, parse_ms, mbs, bound_ms);
    if (parse_ms > bound_ms) {
        std::printf("  *** REGRESSION: %.2f ms exceeds bound %.0f ms ***\n",
                    parse_ms, bound_ms);
        std::exit(1);
    }
}

std::string makeFlatMap(std::size_t n) {
    std::string s;
    s.reserve(n * 24);
    for (std::size_t i = 0; i < n; ++i) {
        s += "key";
        s += std::to_string(i);
        s += ": value";
        s += std::to_string(i);
        s += '\n';
    }
    return s;
}

std::string makeDeepNesting(std::size_t depth) {
    std::string s;
    for (std::size_t i = 0; i < depth; ++i) {
        s += std::string(i * 2, ' ');
        s += "level";
        s += std::to_string(i);
        s += ":\n";
    }
    s += std::string(depth * 2, ' ');
    s += "value: deep\n";
    return s;
}

std::string makeFlowHeavy(std::size_t n) {
    std::string s = "items:\n";
    s.reserve(n * 32);
    for (std::size_t i = 0; i < n; ++i) {
        s += "  - {id: ";
        s += std::to_string(i);
        s += ", name: item";
        s += std::to_string(i);
        s += ", tags: [a, b, c]}\n";
    }
    return s;
}

std::string makeSceneFile() {
    // A realistic scene-config shape (ZeroEngine use case).
    return std::string(
        "# scene config\n"
        "version: \"1.0\"\n"
        "camera:\n"
        "  fov: 60\n"
        "  near: 0.1\n"
        "  far: 1000.0\n"
        "  position: [0.0, 5.0, 10.0]\n"
        "lights:\n"
        "  - type: directional   # sun\n"
        "    intensity: 3.0\n"
        "    color: [1.0, 0.95, 0.85]\n"
        "    direction: [-0.4, -1.0, -0.3]\n"
        "  - type: ambient\n"
        "    intensity: 0.1\n"
        "    color: [0.5, 0.6, 0.7]\n"
        "materials:\n"
        "  floor: {shader: pbr, roughness: 0.8, albedo: [0.5, 0.5, 0.5]}\n"
        "  helmet: {shader: pbr, roughness: 0.3, metalness: 0.9}\n"
        "objects:\n"
        "  - name: floor\n"
        "    mesh: Assets/floor.obj\n"
        "    material: floor\n"
        "  - name: helmet\n"
        "    mesh: Assets/helmet.obj\n"
        "    material: helmet\n"
        "    transform:\n"
        "      position: [0.0, 1.0, 0.0]\n"
        "      rotation: [0.0, 0.0, 0.0]\n"
        "      scale: [1.0, 1.0, 1.0]\n");
}

} // namespace

int main() {
    std::printf("ZYaml benchmark\n");
    std::fflush(stdout);

    // 1. Small scene config (~600 bytes)
    {
        auto scene = makeSceneFile();
        std::printf("  [1/5] scene config (%zu bytes)\n", scene.size());
        std::fflush(stdout);
        auto t0 = clk::now();
        auto doc = zyaml::parse(scene);
        auto t1 = clk::now();
        if (!doc) { std::printf("  scene parse failed: %s\n", doc.error().format().c_str()); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
        report("scene config (~600 B)", t, scene.size(), 50);
    }

    // 2. Large flat map (20000 keys)
    {
        auto big = makeFlatMap(20000);
        std::printf("  [2/5] flat map (%zu bytes)\n", big.size());
        std::fflush(stdout);
        auto t0 = clk::now();
        auto doc = zyaml::parse(big);
        auto t1 = clk::now();
        if (!doc) { std::printf("  flat-map parse failed\n"); return 1; }
        if (doc->root().size() != 20000) { std::printf("  size mismatch\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
        report("flat map (20k keys)", t, big.size(), 500);
    }

    // 3. Deep nesting (50 levels — realistic config depth; parser is
    //    recursive-descent so very deep nesting can blow the stack)
    {
        auto deep = makeDeepNesting(50);
        std::printf("  [3/5] deep nesting (%zu bytes)\n", deep.size());
        std::fflush(stdout);
        auto t0 = clk::now();
        auto doc = zyaml::parse(deep);
        auto t1 = clk::now();
        if (!doc) { std::printf("  deep parse failed\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
        report("deep nesting (50)", t, deep.size(), 100);
    }

    // 4. Flow-heavy (5000 items)
    {
        auto flow = makeFlowHeavy(5000);
        std::printf("  [4/5] flow-heavy (%zu bytes)\n", flow.size());
        std::fflush(stdout);
        auto t0 = clk::now();
        auto doc = zyaml::parse(flow);
        auto t1 = clk::now();
        if (!doc) { std::printf("  flow parse failed\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
        report("flow-heavy (5k items)", t, flow.size(), 500);
    }

    // 5. Round-trip: parse → emit → parse on the flat map
    {
        auto big = makeFlatMap(20000);
        std::printf("  [5/5] round-trip (%zu bytes)\n", big.size());
        std::fflush(stdout);
        auto t0 = clk::now();
        auto doc = zyaml::parse(big);
        if (!doc) return 1;
        std::string emitted = zyaml::emit(doc->root());
        auto re = zyaml::parse(emitted);
        if (!re) return 1;
        auto t1 = clk::now();
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
        report("round-trip (20k map)", t, big.size(), 1000);
    }

    std::printf("  all benchmarks within bounds\n");
    std::fflush(stdout);
    return 0;
}
