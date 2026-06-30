# Tests use CTest. Each test is a small executable that returns 0 on success.
#
# Pattern mirrors ZeroEngine's Test/CMakeLists.txt: one executable per test
# file, linked against ZYaml, with CXX_SCAN_FOR_MODULES ON so `import ZYaml`
# resolves.

add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE ZYaml::ZYaml)
set_target_properties(test_smoke PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_smoke PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(test_smoke PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_smoke COMMAND test_smoke)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE ZYaml::ZYaml)
set_target_properties(test_parser PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_parser PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(test_parser PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_parser COMMAND test_parser)

add_executable(test_sequence test_sequence.cpp)
target_link_libraries(test_sequence PRIVATE ZYaml::ZYaml)
set_target_properties(test_sequence PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_sequence PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(test_sequence PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_sequence COMMAND test_sequence)

add_executable(test_flow test_flow.cpp)
target_link_libraries(test_flow PRIVATE ZYaml::ZYaml)
set_target_properties(test_flow PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_flow PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(test_flow PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_flow COMMAND test_flow)

add_executable(test_convert test_convert.cpp)
target_link_libraries(test_convert PRIVATE ZYaml::ZYaml)
set_target_properties(test_convert PROPERTIES CXX_SCAN_FOR_MODULES ON)
target_compile_features(test_convert PRIVATE cxx_std_23)
if(MSVC)
    target_compile_options(test_convert PRIVATE /utf-8 /W4 /WX /Zc:preprocessor /permissive-)
endif()

add_test(NAME zyaml_convert COMMAND test_convert)
