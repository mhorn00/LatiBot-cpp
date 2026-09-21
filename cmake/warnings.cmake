# Warning and sanitizer flags for our own targets.
#
# Third-party code (DPP, and DECtalk later) is built with its own flags; these
# functions are only applied to targets we write.

function(latibot_target_warnings target)
    if(MSVC)
        # /external:W0 silences warnings from headers CMake passes with
        # /external:I, i.e. dependencies marked SYSTEM.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /external:W0)
        if(LATIBOT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(LATIBOT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# AddressSanitizer, enabled by the "asan" and "fuzz" presets.
#
# MSVC's ASan cannot be combined with the /RTC1 runtime checks that CMake puts
# in the default Debug flags, and it requires incremental linking to be off.
function(latibot_target_sanitizers target)
    if(NOT LATIBOT_ENABLE_ASAN)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /fsanitize=address)
        target_link_options(${target} PRIVATE /INCREMENTAL:NO)
    else()
        target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address)
    endif()
endfunction()

# Strips /RTC1 from the Debug flags globally. Call once, before targets are
# defined, when ASan is on.
function(latibot_strip_runtime_checks)
    if(NOT (LATIBOT_ENABLE_ASAN AND MSVC))
        return()
    endif()
    foreach(flags_var CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
        string(REGEX REPLACE "/RTC[1csu]+" "" stripped "${${flags_var}}")
        set(${flags_var} "${stripped}" CACHE STRING "" FORCE)
    endforeach()
endfunction()
