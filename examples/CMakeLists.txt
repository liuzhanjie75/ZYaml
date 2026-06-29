# Examples are added in later milestones. This file exists so the parent
# CMakeLists' add_subdirectory(examples) succeeds even when empty.
#
# To enable an example later:
#   add_executable(example_basic basic.cpp)
#   target_link_libraries(example_basic PRIVATE ZYaml::ZYaml)
#   set_target_properties(example_basic PROPERTIES CXX_SCAN_FOR_MODULES ON)
