# The triggers module (README.md).
latibot_module(triggers
    SOURCES
        src/module.cpp
        src/triggers.cpp
        src/trigger_command.cpp
        src/trigger_tools.cpp
    TESTS
        tests/triggers_test.cpp
        tests/trigger_command_test.cpp
        tests/trigger_store_test.cpp
        tests/trigger_panel_test.cpp
        tests/triggers_module_test.cpp
        tests/trigger_tools_test.cpp
)
