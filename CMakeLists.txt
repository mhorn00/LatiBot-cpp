cmake_minimum_required(VERSION 3.21)

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
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # only honoured by the Ninja presets

option(LATIBOT_BUILD_TESTS "Build the unit tests" ON)
option(LATIBOT_BUILD_FUZZERS "Build the libFuzzer targets" OFF)
option(LATIBOT_ENABLE_ASAN "Build our targets with AddressSanitizer" OFF)
option(LATIBOT_WARNINGS_AS_ERRORS "Treat warnings in our code as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)
include(helpers)

latibot_strip_runtime_checks()

# Put every built binary (ours and DPP's DLL) in one folder per config.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Opus REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ctre REQUIRED)
latibot_map_conan_configs()

# DPP from source (third_party/DPP), linked against the Conan-provided
# OpenSSL/zlib/opus instead of the outdated binaries bundled in DPP's win32/ folder.
set(CONAN_EXPORTED ON CACHE BOOL "" FORCE)
set(DPP_NO_VCPKG ON CACHE BOOL "" FORCE)
set(DPP_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DPP_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_VOICE_SUPPORT ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
# std::formatter<dpp::snowflake>, so an id can go straight into std::format and
# the logger can see it is an id (core/util/log.hpp colours it by type).
set(DPP_FORMATTERS ON CACHE BOOL "" FORCE)
# DPP only detects opus on Windows via its bundled-binaries path, so voice
# support must be switched on explicitly when using external dependencies.
set(HAVE_OPUS_OPUS_H ON)
set(OPUS_LIBRARIES Opus::opus)
add_subdirectory(third_party/DPP EXCLUDE_FROM_ALL)
# DPP hardcodes OPENSSL_VERSION to 1.1.1f on Windows, which makes mlspp compile
# its OpenSSL 1.1 code path; that path does not build against OpenSSL 3.
target_compile_definitions(hpke PRIVATE WITH_OPENSSL3)
# DPP's public headers are not /W4-clean (C4251 on exported std types, unused
# parameters in templates). We build our own code with warnings as errors, so
# treat DPP's includes as system headers for anything that consumes them.
if(NOT CMAKE_VERSION VERSION_LESS 3.25)
    set_target_properties(dpp PROPERTIES SYSTEM TRUE)
endif()

include(dectalk)

add_subdirectory(src)

if(LATIBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
