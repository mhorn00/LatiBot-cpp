find_package(Catch2 3 REQUIRED)

add_executable(latibot_tests
    support/quiet_log.cpp
    db/backup_test.cpp
    db/bot_allowlist_test.cpp
    db/database_test.cpp
    db/trigger_store_test.cpp
    db/guild_settings_test.cpp
    db/migrations_test.cpp
    db/nickname_store_test.cpp
    unit/basic_commands_test.cpp
    unit/bootstrap_test.cpp
    unit/bots_command_test.cpp
    unit/ca_certificates_test.cpp
    unit/command_log_test.cpp
    unit/env_test.cpp
    unit/goodbye_test.cpp
    unit/message_pipeline_test.cpp
    unit/nickname_command_test.cpp
    unit/nicknames_test.cpp
    unit/paginator_test.cpp
    unit/log_test.cpp
    unit/ports_test.cpp
    unit/preflight_test.cpp
    unit/raw_api_test.cpp
    unit/registry_test.cpp
    unit/trigger_command_test.cpp
    unit/triggers_test.cpp
    unit/version_test.cpp
    unit/text_test.cpp
)

# Test helpers are included as "support/<file>.hpp".
target_include_directories(latibot_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(latibot_tests PRIVATE latibot_core Catch2::Catch2WithMain)

latibot_target_warnings(latibot_tests)
latibot_target_sanitizers(latibot_tests)
latibot_copy_runtime_dlls(latibot_tests)

include(Catch)
# PRE_TEST discovery runs the binary at test time rather than build time, so a
# build never depends on the DLLs already being in place.
catch_discover_tests(latibot_tests DISCOVERY_MODE PRE_TEST)

if(LATIBOT_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
