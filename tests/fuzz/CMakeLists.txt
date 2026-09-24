# libFuzzer targets, built by the "fuzz" preset (LATIBOT_BUILD_FUZZERS=ON).
#
# MSVC's libFuzzer support comes with the AddressSanitizer component, so these
# targets always build with ASan on. Each target takes untrusted text: user
# messages (URL scanner), or text on its way into the DECtalk engine.

function(latibot_add_fuzzer name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE latibot_core)
    latibot_target_warnings(${name})
    if(MSVC)
        target_compile_options(${name} PRIVATE /fsanitize=fuzzer /fsanitize=address)
        target_link_options(${name} PRIVATE /INCREMENTAL:NO)
        # latibot_core is built with the STL's ASan annotations off (see
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

# As the parsers land, add:
#   latibot_add_fuzzer(fuzz_dectalk_sanitizer fuzz_sanitizer.cpp) (plan v4 §12.5)
