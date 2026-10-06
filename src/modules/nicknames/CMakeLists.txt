# The nicknames module (README.md).
latibot_module(nicknames CONFIG
    SOURCES
        src/module.cpp
        src/nicknames.cpp
        src/nickname_import.cpp
        src/nickname_command.cpp
    TESTS
        tests/nicknames_test.cpp
        tests/nickname_command_test.cpp
        tests/nickname_import_test.cpp
        tests/nickname_store_test.cpp
        tests/nickname_import_store_test.cpp
        tests/nicknames_module_test.cpp
)
