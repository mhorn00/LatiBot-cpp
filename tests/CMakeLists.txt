find_package(Catch2 3 REQUIRED)
latibot_map_conan_configs()

add_executable(latibot_tests
    support/quiet_log.cpp
    db/backfill_test.cpp
    db/backup_test.cpp
    db/bot_allowlist_test.cpp
    db/database_test.cpp
    db/emoji_copies_test.cpp
    db/trigger_store_test.cpp
    db/reaction_store_test.cpp
    db/replacement_store_test.cpp
    db/url_rule_store_test.cpp
    db/voice_store_test.cpp
    db/guild_settings_test.cpp
    db/llm_store_test.cpp
    db/media_posts_test.cpp
    db/migrations_test.cpp
    db/midnight_store_test.cpp
    db/nickname_import_test.cpp
    db/nickname_store_test.cpp
    unit/basic_commands_test.cpp
    unit/bootstrap_test.cpp
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
    unit/embed_watch_test.cpp
    unit/env_test.cpp
    unit/goodbye_test.cpp
    unit/legacy_replacements_test.cpp
    unit/message_pipeline_test.cpp
    unit/midnight_command_test.cpp
    unit/midnight_test.cpp
    unit/music_command_test.cpp
    unit/music_links_test.cpp
    unit/music_player_test.cpp
    unit/music_queue_test.cpp
    unit/nickname_command_test.cpp
    unit/nickname_import_test.cpp
    unit/nicknames_test.cpp
    unit/paginator_test.cpp
    unit/panels_test.cpp
    unit/pcm_test.cpp
    unit/linkstats_command_test.cpp
    unit/llm_answer_test.cpp
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

# Test helpers are included as "support/<file>.hpp".
target_include_directories(latibot_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# Where the golden files are (tests/golden), for tests that compare against them.
target_compile_definitions(latibot_tests PRIVATE LATIBOT_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(latibot_tests PRIVATE latibot_core Catch2::Catch2WithMain)

# A program the process tests run in place of yt-dlp and ffmpeg
# (support/test_child.cpp); the tests find it by its path.
add_executable(latibot_test_child support/test_child.cpp)
target_link_libraries(latibot_test_child PRIVATE shell32)
latibot_target_warnings(latibot_test_child)
add_dependencies(latibot_tests latibot_test_child)
target_compile_definitions(latibot_tests PRIVATE LATIBOT_TEST_CHILD="$<TARGET_FILE:latibot_test_child>")

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
