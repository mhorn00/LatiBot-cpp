cmake_minimum_required(VERSION 3.21)

# Conan installs only these two, so no other configuration could link.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(LatiBot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake and Conan both spell C++23 as /std:c++latest for MSVC, which also turns
# on C++26 draft and experimental features that change with every compiler
# update. Ask for C++23 alone: /std:c++23preview up to MSVC Build Tools 14.51,
# and /std:c++23 from 14.52 (cl 19.52), which retires the preview switch. MSVC
# ignores a /std it does not know and compiles as C++14, so the switch follows
# the compiler rather than being written down once.
#
# Only our own code is C++23. DPP sets its own standard in its directory and
# stays C++20 (its mlspp, C++17), DECtalk is C, and the Conan packages are
# installed with compiler.cppstd=20 (README.md, step 4).
if(MSVC)
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.43)
        message(FATAL_ERROR "C++23 needs MSVC 19.43 (Visual Studio 2022 17.13) or newer; this one is ${CMAKE_CXX_COMPILER_VERSION}")
    elseif(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.52)
        set(latibot_cxx23_switch "-std:c++23preview")
    else()
        set(latibot_cxx23_switch "-std:c++23")
    endif()
    set(CMAKE_CXX23_STANDARD_COMPILE_OPTION "${latibot_cxx23_switch}")
    set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION "${latibot_cxx23_switch}")
endif()
# For clang-tidy (tools/Invoke-ClangTidy.ps1), which reads the Debug entries.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LATIBOT_BUILD_TESTS "Build the unit tests" ON)
option(LATIBOT_BUILD_FUZZERS "Build the libFuzzer targets" OFF)
option(LATIBOT_ENABLE_ASAN "Build our targets with AddressSanitizer" OFF)
option(LATIBOT_WARNINGS_AS_ERRORS "Treat warnings in our code as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)
include(helpers)

latibot_strip_runtime_checks()

# Put every built binary, and DECtalk's DLL beside them, in one folder per config.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ctre REQUIRED)
# DPP, built from third_party/DPP by conan/dpp/conanfile.py, once per
# configuration rather than once per build folder.
find_package(dpp REQUIRED)
latibot_map_conan_configs()

include(dectalk)

add_subdirectory(src)

if(LATIBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
