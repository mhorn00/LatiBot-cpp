# Shared helpers for our targets.

# MSVC links ASan's runtime dynamically, and it lives beside cl.exe rather
# than anywhere on PATH, so an instrumented executable fails to start with
# STATUS_DLL_NOT_FOUND. Copy it next to the binary, as we do for dpp.dll.
function(latibot_copy_asan_runtime target)
    if(NOT (LATIBOT_ENABLE_ASAN AND MSVC))
        return()
    endif()

    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "EXECUTABLE")
        return()
    endif()

    get_filename_component(msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_file(LATIBOT_ASAN_RUNTIME
        NAMES clang_rt.asan_dynamic-x86_64.dll
        HINTS "${msvc_bin_dir}"
        NO_DEFAULT_PATH
    )

    if(NOT LATIBOT_ASAN_RUNTIME)
        message(WARNING "AddressSanitizer runtime not found next to ${CMAKE_CXX_COMPILER}. "
                        "Instrumented binaries will not start.")
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LATIBOT_ASAN_RUNTIME}" "$<TARGET_FILE_DIR:${target}>"
    )
endfunction()

# DPP is built as a DLL, so every executable that links it needs the runtime
# DLLs beside it. $<TARGET_RUNTIME_DLLS:...> needs CMake >= 3.21.
function(latibot_copy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
    )
endfunction()
