# Shared helpers for our targets.

# MSVC links ASan's runtime dynamically, and it lives beside cl.exe rather
# than anywhere on PATH, so an instrumented executable fails to start with
# STATUS_DLL_NOT_FOUND. Copy it next to the binary, as we do for dectalk.dll.
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

# Conan's CMakeDeps makes one imported target per library per configuration,
# CONAN_LIB::<package>_<lib>_RELEASE and _DEBUG, each with only its own
# configuration's location. Answering VS Code's codemodel query, CMake 4.4
# asks each for its location in the other configuration too, and reports
# dozens of "IMPORTED_LOCATION not set" errors (README.md, Notes / gotchas).
#
# Pointing each at its own configuration answers that. Only these leaf
# targets are mapped, never the _DEPS_TARGET that links them: that one picks
# a configuration's libraries with $<CONFIG:...>, which honours a mapping, so
# mapping it would link Release libraries into Debug (a global
# CMAKE_MAP_IMPORTED_CONFIG_<CONFIG> did exactly that). What is built does not
# change: the generated projects are byte for byte the same with and without
# this.
#
# Imported targets belong to the directory whose find_package made them, so
# call this after the find_package calls in each such directory.
function(latibot_map_conan_configs)
    get_property(imported DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(target IN LISTS imported)
        if(target MATCHES "^CONAN_LIB::.*_RELEASE$")
            set_property(TARGET ${target} PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
        elseif(target MATCHES "^CONAN_LIB::.*_DEBUG$")
            set_property(TARGET ${target} PROPERTY MAP_IMPORTED_CONFIG_RELEASE Debug)
        endif()
    endforeach()
endfunction()

# DECtalk is a DLL, so every executable that links it needs it beside it.
# $<TARGET_RUNTIME_DLLS:...> needs CMake >= 3.21.
function(latibot_copy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
    )
endfunction()
