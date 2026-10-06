# Every feature module, in dependency order: each after the modules it
# requires (docs/modules/Module_Plan_Final.md §3), which is also the order the
# bot builds them in. Each is on unless its switch is off; one whose
# requirement is off stops the configure, naming both (cmake/modules.cmake).

option(LATIBOT_WITH_MIDNIGHT "Build the midnight module: /midnight and the daily message" ON)
if(LATIBOT_WITH_MIDNIGHT)
    add_subdirectory(midnight)
endif()
