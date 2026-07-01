# Benchmark — parse/emit throughput on representative shapes.
#
# Built as a ctest that asserts performance bounds. Build with Release
# for meaningful numbers:
#   cmake -S . -B build_rel -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build_rel
#   ctest --test-dir build_rel -R bench --output-on-failure
#
# In Debug the bounds are still enforced but ~10x looser than Release
# throughput, so Debug runs may flake on the flat-map bound.

add_executable(zyaml_bench bench.cpp)
target_link_libraries(zyaml_bench PRIVATE ZYaml::ZYaml)
set_target_properties(zyaml_bench PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(zyaml_bench PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(zyaml_bench PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_bench COMMAND zyaml_bench)
