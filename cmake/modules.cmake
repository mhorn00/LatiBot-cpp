# Feature modules (docs/modules/Module_Plan_Final.md §6.3).
#
#   latibot_module(<name>
#       SOURCES  <file>...      # src/..., the library's own
#       REQUIRES <module>...    # modules it is built against, already added
#       LINKS    <target>...    # libraries only this module needs
#       TESTS    <file>...)     # tests/..., its test executable
#
# Makes the static library latibot_<name>. Its public headers are in
# include/<name>/, which the modules that require it see; its private ones are
# in src/, which only it and its tests see, so a module that reaches into
# another's src/ does not compile. It links the core, and the modules it
# requires, publicly.
#
# Every module has a public include/<name>/module.hpp declaring
# `latibot::<name>::make_module(modules::host&)`, which the generated module
# list calls (latibot_write_module_list).
#
# Whether a module is built is its switch, LATIBOT_WITH_<NAME>, which
# src/modules/CMakeLists.txt checks before adding it. Modules are added in
# dependency order, so that is the order the bot builds them in.

set_property(GLOBAL PROPERTY LATIBOT_MODULES "")

function(latibot_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;REQUIRES;LINKS;TESTS")
    string(TOUPPER "${name}" upper)

    foreach(required IN LISTS arg_REQUIRES)
        string(TOUPPER "${required}" required_upper)
        if(NOT TARGET latibot_${required})
            message(FATAL_ERROR "The ${name} module requires the ${required} module, which is not built. "
                                "Turn LATIBOT_WITH_${required_upper} on, or LATIBOT_WITH_${upper} off.")
        endif()
    endforeach()

    set(target latibot_${name})
    add_library(${target} STATIC ${arg_SOURCES})
    target_include_directories(${target}
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(${target} PUBLIC latibot_core)
    foreach(required IN LISTS arg_REQUIRES)
        target_link_libraries(${target} PUBLIC latibot_${required})
    endforeach()
    if(arg_LINKS)
        target_link_libraries(${target} PRIVATE ${arg_LINKS})
    endif()
    target_precompile_headers(${target} PRIVATE <dpp/dpp.h>)
    latibot_target_warnings(${target})
    latibot_target_sanitizers(${target})

    set_property(GLOBAL APPEND PROPERTY LATIBOT_MODULES ${name})

    # Its tests are made in tests/CMakeLists.txt, where the shared test
    # support is: what they are, and where the module's private headers are.
    list(TRANSFORM arg_TESTS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
    set_property(GLOBAL PROPERTY LATIBOT_MODULE_TESTS_${name} "${arg_TESTS}")
    set_property(GLOBAL PROPERTY LATIBOT_MODULE_PRIVATE_${name} "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set_property(GLOBAL PROPERTY LATIBOT_MODULE_DIR_${name} "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

# Writes <output>: `modules::enabled_modules`, which builds every module this
# build includes, in the order they were added (docs/modules/Module_Plan_Final.md
# §4.8). Rewritten only when the list changes, so a reconfigure that changes
# nothing rebuilds nothing.
function(latibot_write_module_list output)
    get_property(names GLOBAL PROPERTY LATIBOT_MODULES)

    set(includes "")
    set(calls "")
    foreach(name IN LISTS names)
        string(APPEND includes "#include \"${name}/module.hpp\"\n")
        string(APPEND calls "    modules.push_back(${name}::make_module(bot));\n")
    endforeach()
    if(NOT names)
        set(calls "    static_cast<void>(bot);\n")
    endif()

    list(JOIN names ", " listed)
    if(NOT listed)
        set(listed "none")
    endif()

    file(CONFIGURE OUTPUT "${output}" CONTENT [=[
// Written by CMake (cmake/modules.cmake) from the LATIBOT_WITH_* switches;
// edits are lost on the next configure. Modules: @listed@.

#include "core/modules/module.hpp"

@includes@
namespace latibot::modules {

auto enabled_modules(host& bot) -> module_list {
    module_list modules;
@calls@    return modules;
}

} // namespace latibot::modules
]=] @ONLY)
endfunction()
