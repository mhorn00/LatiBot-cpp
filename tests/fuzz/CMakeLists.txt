# libFuzzer targets, built by the "fuzz" preset (LATIBOT_BUILD_FUZZERS=ON).
#
# MSVC's libFuzzer support comes with the AddressSanitizer component, so these
# targets always build with ASan on. Each target takes untrusted text: user
# messages (URL scanner), or text on its way into the DECtalk engine.

# libFuzzer steers by coverage: inputs that reach new code are kept and
# mutated further. Only code built with coverage instrumentation reports any,
# and /fsanitize=fuzzer on a fuzz target covers just its own harness. The code
# under test is in latibot_core, which cannot carry the counters: the bot and
# the unit tests link it without libFuzzer's runtime. So the fuzz targets link
# their own library of the code they exercise and what that needs to link,
# built with the instrumentation. It is a short list rather than all of
# latibot_core because MSVC 14.51 crashes instrumenting bot.cpp, and nothing
# here reaches it.
set(core "${PROJECT_SOURCE_DIR}/src/core")
add_library(latibot_fuzz_core STATIC
    # Fuzzed.
    ${core}/util/text.cpp
    ${core}/util/url_scan.cpp
    ${core}/events/url_rules.cpp
    ${core}/events/legacy_replacements.cpp
    ${core}/audio/dectalk_sanitizer.cpp
    # What those need to link.
    ${core}/config/guild_settings.cpp
    ${core}/db/database.cpp
    ${core}/db/error.cpp
    ${core}/db/statement.cpp
    ${core}/util/env.cpp
    ${core}/util/log.cpp
)
target_include_directories(latibot_fuzz_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(latibot_fuzz_core PUBLIC dpp SQLite::SQLite3 ctre::ctre)
latibot_target_warnings(latibot_fuzz_core)
latibot_target_sanitizers(latibot_fuzz_core)
if(MSVC)
    target_compile_options(latibot_fuzz_core PRIVATE /fsanitize-coverage=inline-8bit-counters /fsanitize-coverage=edge
                                                     /fsanitize-coverage=trace-cmp /fsanitize-coverage=trace-div)
else()
    target_compile_options(latibot_fuzz_core PRIVATE -fsanitize=fuzzer-no-link)
endif()

function(latibot_add_fuzzer name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE latibot_fuzz_core)
    latibot_target_warnings(${name})
    if(MSVC)
        target_compile_options(${name} PRIVATE /fsanitize=fuzzer /fsanitize=address)
        target_link_options(${name} PRIVATE /INCREMENTAL:NO)
        # The library is built with the STL's ASan annotations off (see
        # latibot_target_sanitizers), and MSVC refuses to link objects that
        # disagree. fuzz_text only got away without this because nothing it
        # pulls in touches std::string.
        target_compile_definitions(${name} PRIVATE _DISABLE_STL_ANNOTATION)
    else()
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer,address)
    endif()
    latibot_copy_runtime_dlls(${name})
endfunction()

latibot_add_fuzzer(fuzz_text fuzz_text.cpp)
latibot_add_fuzzer(fuzz_url_scan fuzz_url_scan.cpp)
latibot_add_fuzzer(fuzz_legacy_parser fuzz_legacy_parser.cpp)
latibot_add_fuzzer(fuzz_dectalk_sanitizer fuzz_dectalk_sanitizer.cpp)

