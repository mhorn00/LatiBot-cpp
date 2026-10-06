# Builds third_party/DPP the way LatiBot needs it, for conan/dpp/conanfile.py.
#
# What the root CMakeLists.txt used to do around add_subdirectory(DPP),
# unchanged: DPP links Conan's OpenSSL, zlib and opus instead of the outdated
# binaries it bundles for Windows, with voice support on and DPP's
# std::formatter for snowflakes.
cmake_minimum_required(VERSION 3.21)

project(dpp_for_latibot LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Opus REQUIRED)

set(CONAN_EXPORTED ON CACHE BOOL "" FORCE)
set(DPP_NO_VCPKG ON CACHE BOOL "" FORCE)
set(DPP_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(DPP_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_VOICE_SUPPORT ON CACHE BOOL "" FORCE)
# std::formatter<dpp::snowflake>, so an id can go straight into std::format
# and LatiBot's logger can see it is an id.
set(DPP_FORMATTERS ON CACHE BOOL "" FORCE)
# DPP only detects opus on Windows through its bundled binaries, so voice
# support is switched on explicitly when the dependencies are external.
set(HAVE_OPUS_OPUS_H ON)
set(OPUS_LIBRARIES Opus::opus)

add_subdirectory(DPP)

# DPP hardcodes OPENSSL_VERSION to 1.1.1f on Windows, which makes mlspp
# compile its OpenSSL 1.1 code path; that path does not build against
# OpenSSL 3.
target_compile_definitions(hpke PRIVATE WITH_OPENSSL3)
