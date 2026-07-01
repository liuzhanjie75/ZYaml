# Benchmark — parse/emit throughput on representative shapes.
#
# Opt-in (ZYAML_BUILD_BENCH, default OFF). Bounds are calibrated for
# Release; the flat-map case flakes in Debug (~252 ms Debug vs ~15 ms
# Release against a 500 ms bound). When enabled, the test is labelled
# "bench" so CI can run it separately from the unit tests:
#   ctest --test-dir build -L bench          # run only benchmarks
#   ctest --test-dir build -LE bench          # run everything but benchmarks
#
# Recommended invocation (Release):
#   cmake -S . -B build_rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DZYAML_BUILD_BENCH=ON
#   cmake --build build_rel
#   ctest --test-dir build_rel -L bench --output-on-failure

add_executable(zyaml_bench bench.cpp)
target_link_libraries(zyaml_bench PRIVATE ZYaml::ZYaml)
set_target_properties(zyaml_bench PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(zyaml_bench PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(zyaml_bench PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_bench COMMAND zyaml_bench)
set_tests_properties(zyaml_bench PROPERTIES LABELS "bench")
