cmake_minimum_required(VERSION 3.21)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(LatiBot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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

# DPP from source (third_party/DPP), linked against the Conan-provided
# OpenSSL/zlib/opus instead of the outdated binaries bundled in DPP's win32/ folder.
set(CONAN_EXPORTED ON CACHE BOOL "" FORCE)
set(DPP_NO_VCPKG ON CACHE BOOL "" FORCE)
set(DPP_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DPP_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_VOICE_SUPPORT ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
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

add_subdirectory(src)

if(LATIBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
