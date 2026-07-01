// ZYaml benchmark — parse/emit throughput + allocation counts on
// representative shapes.
//
// Prints timings + allocation counts for: small config, large flat map
// (the O(n^2) regression target), deep nesting, flow-heavy, a realistic
// scene-file corpus, and a full round-trip. Asserts generous upper bounds
// to catch performance regressions in CI. Bounds are ~10x the measured
// Release numbers to avoid flaky failures.
//
// Allocation counts use a global operator new interceptor — tracks every
// heap allocation during parse/emit. Useful for catching regressions in
// Node storage (e.g. if the hash side-index starts over-allocating).
//
// Build: cmake --build build --config Release (or -DCMAKE_BUILD_TYPE=Release)
// Run:   ./bench/zyaml_bench

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <vector>

import ZYaml;

// ── Allocation counter ───────────────────────────────────────────────
// A global operator new interceptor that counts allocations and total
// bytes. Thread-safe via atomic. opt-out via ZYAML_NO_ALLOC_COUNTER to
// avoid symbol collision if a consumer overrides operator new.
#ifndef ZYAML_NO_ALLOC_COUNTER
namespace {
std::atomic<std::size_t> g_alloc_count{0};
std::atomic<std::size_t> g_alloc_bytes{0};
}

void* operator new(std::size_t n) {
    g_alloc_count.fetch_add(1, std::memory_order_relaxed);
    g_alloc_bytes.fetch_add(n, std::memory_order_relaxed);
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct AllocTracker {
    std::size_t count, bytes;
    void reset() {
        g_alloc_count.store(0, std::memory_order_relaxed);
        g_alloc_bytes.store(0, std::memory_order_relaxed);
    }
    void snapshot() {
        count = g_alloc_count.load(std::memory_order_relaxed);
        bytes = g_alloc_bytes.load(std::memory_order_relaxed);
    }
};
#endif

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
            double bound_ms, std::size_t allocs) {
    auto mbs = bytes / (parse_ms / 1000.0) / (1024.0 * 1024.0);
    std::printf("  %-28s %7.2f ms  %6.2f MB/s  %6zu allocs  (bound %.0f ms)\n",
                label, parse_ms, mbs, allocs, bound_ms);
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
    // A realistic scene-config shape (ZeroEngine use case) — small.
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

// Realistic mid-size corpus: a scene file with many objects, nested
// transforms, material references, and a mix of block + flow. Mimics a
// real game-engine scene file (the ZeroEngine target use case), not a
// synthetic worst case. ~50 KB.
std::string makeRealisticScene(std::size_t num_objects) {
    std::string s;
    s.reserve(num_objects * 200);
    s += "# auto-generated realistic scene corpus\n";
    s += "version: \"1.0\"\n";
    s += "camera:\n  fov: 60\n  near: 0.1\n  far: 1000.0\n";
    s += "  position: [0.0, 5.0, 10.0]\n";
    s += "lights:\n";
    s += "  - {type: directional, intensity: 3.0, color: [1.0, 0.95, 0.85]}\n";
    s += "  - {type: ambient, intensity: 0.1, color: [0.5, 0.6, 0.7]}\n";
    s += "materials:\n";
    s += "  default: {shader: pbr, roughness: 0.5, metalness: 0.0}\n";
    s += "objects:\n";
    for (std::size_t i = 0; i < num_objects; ++i) {
        s += "  - name: object_";
        s += std::to_string(i);
        s += "\n    mesh: Assets/mesh_";
        s += std::to_string(i);
        s += ".obj\n    material: default\n";
        s += "    transform:\n";
        s += "      position: [";
        s += std::to_string(i * 1.5);
        s += ", 0.0, ";
        s += std::to_string(i * 0.5);
        s += "]\n      rotation: [0.0, ";
        s += std::to_string(i * 15.0);
        s += ", 0.0]\n      scale: [1.0, 1.0, 1.0]\n";
        s += "    properties:\n";
        s += "      cast_shadow: true\n";
        s += "      receive_shadow: true\n";
        s += "      lod: ";
        s += std::to_string(i % 4);
        s += "\n";
    }
    return s;
}

} // namespace

int main() {
    std::printf("ZYaml benchmark\n");
    std::fflush(stdout);

#ifndef ZYAML_NO_ALLOC_COUNTER
    AllocTracker tracker;
    tracker.reset();
#else
    struct { void reset() {} void snapshot() {} } tracker;
    constexpr std::size_t allocs_dummy = 0;
#endif

    // 1. Small scene config (~600 bytes)
    {
        auto scene = makeSceneFile();
        std::printf("  [1/6] scene config (%zu bytes)\n", scene.size());
        std::fflush(stdout);
        tracker.reset();
        auto t0 = clk::now();
        auto doc = zyaml::parse(scene);
        auto t1 = clk::now();
        tracker.snapshot();
        if (!doc) { std::printf("  scene parse failed: %s\n", doc.error().format().c_str()); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
#ifdef ZYAML_NO_ALLOC_COUNTER
        report("scene config (~600 B)", t, scene.size(), 50, 0);
#else
        report("scene config (~600 B)", t, scene.size(), 50, tracker.count);
#endif
    }

    // 2. Large flat map (20000 keys)
    {
        auto big = makeFlatMap(20000);
        std::printf("  [2/6] flat map (%zu bytes)\n", big.size());
        std::fflush(stdout);
        tracker.reset();
        auto t0 = clk::now();
        auto doc = zyaml::parse(big);
        auto t1 = clk::now();
        tracker.snapshot();
        if (!doc) { std::printf("  flat-map parse failed\n"); return 1; }
        if (doc->root().size() != 20000) { std::printf("  size mismatch\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
#ifdef ZYAML_NO_ALLOC_COUNTER
        report("flat map (20k keys)", t, big.size(), 500, 0);
#else
        report("flat map (20k keys)", t, big.size(), 500, tracker.count);
#endif
    }

    // 3. Deep nesting (50 levels — realistic config depth; parser is
    //    recursive-descent so very deep nesting can blow the stack)
    {
        auto deep = makeDeepNesting(50);
        std::printf("  [3/6] deep nesting (%zu bytes)\n", deep.size());
        std::fflush(stdout);
        tracker.reset();
        auto t0 = clk::now();
        auto doc = zyaml::parse(deep);
        auto t1 = clk::now();
        tracker.snapshot();
        if (!doc) { std::printf("  deep parse failed\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
#ifdef ZYAML_NO_ALLOC_COUNTER
        report("deep nesting (50)", t, deep.size(), 100, 0);
#else
        report("deep nesting (50)", t, deep.size(), 100, tracker.count);
#endif
    }

    // 4. Flow-heavy (5000 items)
    {
        auto flow = makeFlowHeavy(5000);
        std::printf("  [4/6] flow-heavy (%zu bytes)\n", flow.size());
        std::fflush(stdout);
        tracker.reset();
        auto t0 = clk::now();
        auto doc = zyaml::parse(flow);
        auto t1 = clk::now();
        tracker.snapshot();
        if (!doc) { std::printf("  flow parse failed\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
#ifdef ZYAML_NO_ALLOC_COUNTER
        report("flow-heavy (5k items)", t, flow.size(), 500, 0);
#else
        report("flow-heavy (5k items)", t, flow.size(), 500, tracker.count);
#endif
    }

    // 5. Realistic scene corpus (~50 KB, 500 objects)
    {
        auto scene = makeRealisticScene(500);
        std::printf("  [5/6] realistic scene (%zu bytes, 500 objects)\n", scene.size());
        std::fflush(stdout);
        tracker.reset();
        auto t0 = clk::now();
        auto doc = zyaml::parse(scene);
        auto t1 = clk::now();
        tracker.snapshot();
        if (!doc) { std::printf("  scene parse failed\n"); return 1; }
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
#ifdef ZYAML_NO_ALLOC_COUNTER
        report("realistic scene (500 obj)", t, scene.size(), 300, 0);
#else
        report("realistic scene (500 obj)", t, scene.size(), 300, tracker.count);
#endif
    }

    // 6. Round-trip: parse → emit → parse on the flat map
    {
        auto big = makeFlatMap(20000);
        std::printf("  [6/6] round-trip (%zu bytes)\n", big.size());
        std::fflush(stdout);
        tracker.reset();
        auto t0 = clk::now();
        auto doc = zyaml::parse(big);
        if (!doc) return 1;
        std::string emitted = zyaml::emit(doc->root());
        auto re = zyaml::parse(emitted);
        if (!re) return 1;
        auto t1 = clk::now();
        tracker.snapshot();
        double t = std::chrono::duration<double, std::milli>(t1 - t0).count();
#ifdef ZYAML_NO_ALLOC_COUNTER
        report("round-trip (20k map)", t, big.size(), 1000, 0);
#else
        report("round-trip (20k map)", t, big.size(), 1000, tracker.count);
#endif
    }

    std::printf("  all benchmarks within bounds\n");
    std::fflush(stdout);
    return 0;
}
