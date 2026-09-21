# Shared helpers for our targets.

# DPP is built as a DLL, so every executable that links it needs the runtime
# DLLs beside it. $<TARGET_RUNTIME_DLLS:...> needs CMake >= 3.21.
function(latibot_copy_runtime_dlls target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
    )
endfunction()
