cmake_minimum_required(VERSION 4.0)

project(ZYaml
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Modern C++23 YAML 1.2 core library (modules, comment-preserving)"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reject stale USE_MODULES=OFF from old caches — this library is module-only.
if(DEFINED USE_MODULES AND NOT USE_MODULES)
    message(FATAL_ERROR
        "USE_MODULES=OFF is not supported. ZYaml is module-only. "
        "Remove USE_MODULES from your cache and reconfigure.")
endif()

# Module scanning is required for both MSVC and Clang (with clang-scan-deps).
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
        find_program(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS NAMES clang-scan-deps)
    endif()
    if(NOT CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS)
        message(FATAL_ERROR
            "clang-scan-deps not found. Install LLVM/Clang with scan-deps, or set "
            "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS to a valid path.")
    endif()
    message(STATUS "ZYaml: clang-scan-deps: ${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}")
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
else()
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER_ID} is not supported for ZYaml. Use MSVC 19.44+ "
        "or Clang 20+ with clang-scan-deps.")
endif()

message(STATUS "ZYaml: CMAKE_CXX_SCAN_FOR_MODULES=${CMAKE_CXX_SCAN_FOR_MODULES}")

option(ZYAML_BUILD_TESTS "Build ZYaml unit tests" ON)
option(ZYAML_BUILD_EXAMPLES "Build ZYaml examples" ON)

# Warnings as errors for the library itself; consumers opt in via ZYaml::ZYaml.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(ZYAML_WARNING_OPTIONS /W4 /WX /Zc:preprocessor /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(ZYAML_WARNING_OPTIONS -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ZYaml STATIC)
add_library(ZYaml::ZYaml ALIAS ZYaml)

target_sources(ZYaml
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
        FILES
            src/zyaml.cppm
            src/error.ixx
            src/node.ixx
            src/scanner.ixx
            src/parser.ixx
)

target_include_directories(ZYaml PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

if(ZYAML_WARNING_OPTIONS)
    target_compile_options(ZYaml PRIVATE ${ZYAML_WARNING_OPTIONS})
endif()

if(ZYAML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ZYAML_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

message(STATUS "ZYaml ${PROJECT_VERSION} configured (${CMAKE_CXX_COMPILER_ID})")
