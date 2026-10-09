# The core: latibot_core (core/CMakeLists.txt).
add_subdirectory(core)

# The feature modules, each its own library (modules/CMakeLists.txt).
add_subdirectory(modules)

# The list of modules this build includes, `modules::enabled_modules`, and
# every module's library: what the executable is built from, and the app tests
# (tests/app) with it (docs/modules/Module_Plan_Final.md §4.8).
latibot_write_module_list("${CMAKE_CURRENT_BINARY_DIR}/enabled_modules.cpp")
add_library(latibot_enabled_modules INTERFACE)
target_sources(latibot_enabled_modules INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/enabled_modules.cpp")
get_property(latibot_modules GLOBAL PROPERTY LATIBOT_MODULES)
foreach(name IN LISTS latibot_modules)
    target_link_libraries(latibot_enabled_modules INTERFACE latibot_${name})
endforeach()
target_link_libraries(latibot_enabled_modules INTERFACE latibot_core)

add_executable(LatiBot app/main.cpp)
target_link_libraries(LatiBot PRIVATE latibot_enabled_modules)
# The host itself, the command line and unregistering are the core's private
# headers, which the executable alone may use.
target_include_directories(LatiBot PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/core/src")

latibot_target_warnings(LatiBot)
latibot_target_sanitizers(LatiBot)
latibot_copy_runtime_dlls(LatiBot)

# The script that sets up a server for the bot, and its readme, beside the
# bot, so a build output folder is ready to copy to one
# (deploy/Install-Dependencies.md). Copied whenever either changes, not only
# when the bot is linked.
get_property(latibot_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(latibot_multi_config)
    set(LATIBOT_DEPLOY_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>")
else()
    set(LATIBOT_DEPLOY_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()
# Off Windows, the shell script that does the same.
if(WIN32)
    set(LATIBOT_DEPLOY_SCRIPT Install-Dependencies.ps1)
else()
    set(LATIBOT_DEPLOY_SCRIPT install-dependencies.sh)
endif()
set(LATIBOT_DEPLOY_SOURCES
    "${PROJECT_SOURCE_DIR}/deploy/${LATIBOT_DEPLOY_SCRIPT}"
    "${PROJECT_SOURCE_DIR}/deploy/Install-Dependencies.md"
)
add_custom_command(
    OUTPUT "${LATIBOT_DEPLOY_DIR}/${LATIBOT_DEPLOY_SCRIPT}" "${LATIBOT_DEPLOY_DIR}/Install-Dependencies.md"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LATIBOT_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LATIBOT_DEPLOY_SOURCES} "${LATIBOT_DEPLOY_DIR}"
    DEPENDS ${LATIBOT_DEPLOY_SOURCES}
    COMMENT "Copying ${LATIBOT_DEPLOY_SCRIPT} beside the bot"
    VERBATIM
)
add_custom_target(latibot_deploy_files ALL
    DEPENDS "${LATIBOT_DEPLOY_DIR}/${LATIBOT_DEPLOY_SCRIPT}" "${LATIBOT_DEPLOY_DIR}/Install-Dependencies.md"
)
add_dependencies(LatiBot latibot_deploy_files)

# The folder a server needs: `cmake --install build/build --config Release`
# writes it to out/LatiBot-Release (CMakePresets.json, installDir; the VS Code
# task for Debug passes --prefix out/LatiBot-Debug), so the bot that
# runs is never the one being rebuilt (docs/modules/Module_Plan_Final.md §9.1,
# I5). README.md, "On another machine", says what else goes beside it.
install(TARGETS LatiBot RUNTIME DESTINATION .)
install(PROGRAMS "${PROJECT_SOURCE_DIR}/deploy/${LATIBOT_DEPLOY_SCRIPT}" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/deploy/Install-Dependencies.md" DESTINATION .)
