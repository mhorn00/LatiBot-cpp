find_package(Catch2 3 REQUIRED)
latibot_map_conan_configs()
include(Catch)

# What every test executable shares: the helpers in support/ and mocks/,
# included as "support/<file>.hpp" and "mocks/<file>.hpp", the golden files'
# folder, and the listener that quiets the log. An INTERFACE library, so
# quiet_log.cpp is compiled into each executable, where its listener
# registers: a static library would leave it out, since nothing calls it.
add_library(latibot_test_support INTERFACE)
target_sources(latibot_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/support/quiet_log.cpp")
target_include_directories(latibot_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(latibot_test_support INTERFACE LATIBOT_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(latibot_test_support INTERFACE latibot_core Catch2::Catch2WithMain)

# A program the process tests run in place of yt-dlp and ffmpeg
# (support/test_child.cpp); the tests find it by its path.
add_executable(latibot_test_child support/test_child.cpp)
target_link_libraries(latibot_test_child PRIVATE shell32)
latibot_target_warnings(latibot_test_child)

# Builds every test executable: the Testing sidebar's runTask
# (.vscode/settings.json) and the "Build tests (Debug)" task build this.
add_custom_target(latibot_all_tests)

# One test executable, set up as they all are. PREFIX goes before each test's
# name in ctest, which needs every name unique across the executables.
function(latibot_test_executable target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "PREFIX" "SOURCES;LINKS;PRIVATE_INCLUDES")
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE latibot_test_support ${arg_LINKS})
    if(arg_PRIVATE_INCLUDES)
        target_include_directories(${target} PRIVATE ${arg_PRIVATE_INCLUDES})
    endif()
    add_dependencies(${target} latibot_test_child)
    target_compile_definitions(${target} PRIVATE LATIBOT_TEST_CHILD="$<TARGET_FILE:latibot_test_child>")

    # Its own precompiled header rather than a library's: the tests' extra
    # definitions would make MSVC refuse to share one (src/CMakeLists.txt
    # says why there is one at all).
    target_precompile_headers(${target} PRIVATE <dpp/dpp.h> <catch2/catch_test_macros.hpp>)
    latibot_target_warnings(${target})
    latibot_target_sanitizers(${target})
    latibot_copy_runtime_dlls(${target})
    add_dependencies(latibot_all_tests ${target})

    # PRE_TEST discovery runs the binary at test time rather than build time,
    # so a build never depends on the DLLs already being in place.
    if(arg_PREFIX)
        catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST TEST_PREFIX "${arg_PREFIX}")
    else()
        catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
    endif()
endfunction()

# The core, and the features not yet moved into modules.
latibot_test_executable(latibot_tests SOURCES
    db/backfill_test.cpp
    db/backup_test.cpp
    db/bot_allowlist_test.cpp
    db/database_test.cpp
    db/emoji_copies_test.cpp
    db/emote_reactions_test.cpp
    db/trigger_store_test.cpp
    db/reaction_store_test.cpp
    db/replacement_store_test.cpp
    db/url_rule_store_test.cpp
    db/voice_store_test.cpp
    db/guild_settings_test.cpp
    db/llm_store_test.cpp
    db/media_posts_test.cpp
    db/migrations_test.cpp
    db/schema_versions_test.cpp
    db/nickname_import_test.cpp
    db/nickname_store_test.cpp
    unit/basic_commands_test.cpp
    unit/bootstrap_test.cpp
    unit/config_section_test.cpp
    unit/bots_command_test.cpp
    unit/chat_command_test.cpp
    unit/ca_certificates_test.cpp
    unit/command_line_test.cpp
    unit/command_log_test.cpp
    unit/command_options_test.cpp
    unit/command_responses_test.cpp
    unit/custom_voice_test.cpp
    unit/dectalk_engine_test.cpp
    unit/dectalk_golden_test.cpp
    unit/dectalk_sanitizer_test.cpp
    unit/dectalk_speech_test.cpp
    unit/embed_watch_test.cpp
    unit/env_test.cpp
    unit/goodbye_test.cpp
    unit/legacy_replacements_test.cpp
    unit/message_pipeline_test.cpp
    unit/module_test.cpp
    unit/music_command_test.cpp
    unit/music_cookies_test.cpp
    unit/music_links_test.cpp
    unit/music_player_test.cpp
    unit/music_queue_test.cpp
    unit/pot_provider_test.cpp
    unit/nickname_command_test.cpp
    unit/nickname_import_test.cpp
    unit/nicknames_test.cpp
    unit/paginator_test.cpp
    unit/panels_test.cpp
    unit/panel_routes_test.cpp
    unit/pcm_test.cpp
    unit/linkstats_command_test.cpp
    unit/llm_answer_test.cpp
    unit/llm_aliases_test.cpp
    unit/llm_command_test.cpp
    unit/llm_guards_test.cpp
    unit/llm_provider_test.cpp
    unit/llm_tools_test.cpp
    unit/logs_command_test.cpp
    unit/log_channel_test.cpp
    unit/log_color_test.cpp
    unit/log_test.cpp
    unit/message_flags_test.cpp
    unit/ports_test.cpp
    unit/preflight_test.cpp
    unit/process_test.cpp
    unit/raw_api_test.cpp
    unit/reactions_test.cpp
    unit/registry_test.cpp
    unit/speak_command_test.cpp
    unit/speech_queue_test.cpp
    unit/trigger_command_test.cpp
    unit/triggers_test.cpp
    unit/unregister_test.cpp
    unit/version_test.cpp
    unit/voice_lab_test.cpp
    unit/voice_mixer_test.cpp
    unit/voice_params_test.cpp
    unit/voice_sessions_test.cpp
    unit/wav_test.cpp
    unit/yt_dlp_live_test.cpp
    unit/yt_dlp_test.cpp
    unit/text_test.cpp
    unit/url_rules_test.cpp
    unit/urlrepl_command_test.cpp
    unit/url_scan_test.cpp
)

# Each module's own, latibot_<name>_tests, which sees the module's private
# headers and nothing of any module it does not require
# (docs/modules/Module_Plan_Final.md §10).
get_property(latibot_modules GLOBAL PROPERTY LATIBOT_MODULES)
foreach(name IN LISTS latibot_modules)
    get_property(sources GLOBAL PROPERTY LATIBOT_MODULE_TESTS_${name})
    get_property(private GLOBAL PROPERTY LATIBOT_MODULE_PRIVATE_${name})
    get_property(folder GLOBAL PROPERTY LATIBOT_MODULE_DIR_${name})
    if(sources)
        latibot_test_executable(latibot_${name}_tests PREFIX "${name}: " SOURCES ${sources} LINKS latibot_${name}
                                PRIVATE_INCLUDES "${private}")
        # Where its README is, which its tests keep in step with the code.
        target_compile_definitions(latibot_${name}_tests PRIVATE LATIBOT_MODULE_DIR="${folder}")
    endif()
endforeach()

# The bot as this build makes it: every module, through the generated list
# the executable uses (src/CMakeLists.txt).
latibot_test_executable(latibot_app_tests PREFIX "app: " SOURCES app/modules_test.cpp LINKS latibot_enabled_modules)

if(LATIBOT_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
