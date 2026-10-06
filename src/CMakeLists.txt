add_library(latibot_core STATIC
    core/version.cpp
    core/audio/dectalk_engine.cpp
    core/audio/dectalk_sanitizer.cpp
    core/audio/pcm.cpp
    core/audio/speech_queue.cpp
    core/audio/voice_mixer.cpp
    core/audio/voice_params.cpp
    core/audio/voice_store.cpp
    core/audio/wav.cpp
    core/bot.cpp
    core/config/bootstrap.cpp
    core/config/command_line.cpp
    core/config/guild_settings.cpp
    core/db/backup.cpp
    core/db/database.cpp
    core/db/error.cpp
    core/db/migrations.cpp
    core/db/statement.cpp
    core/commands/basic.cpp
    core/commands/linkstats.cpp
    core/commands/llm.cpp
    core/commands/logs.cpp
    core/commands/message_options.cpp
    core/commands/options.cpp
    core/commands/bots.cpp
    core/commands/chat.cpp
    core/commands/midnight.cpp
    core/commands/music.cpp
    core/commands/nickname.cpp
    core/commands/trigger.cpp
    core/commands/urlrepl.cpp
    core/commands/preflight.cpp
    core/commands/registry.cpp
    core/commands/speak.cpp
    core/commands/unregister.cpp
    core/commands/voice.cpp
    core/commands/voice_lab.cpp
    core/discord/dpp_gateway.cpp
    core/discord/dpp_http_client.cpp
    core/discord/dpp_log.cpp
    core/discord/dpp_voice_output.cpp
    core/discord/message_flags.cpp
    core/discord/raw_api.cpp
    core/discord/unregister_commands.cpp
    core/discord/voice_state.cpp
    core/events/backfill.cpp
    core/events/bot_allowlist.cpp
    core/events/emote_reactions.cpp
    core/events/goodbye.cpp
    core/events/legacy_replacements.cpp
    core/events/log_channel.cpp
    core/events/media_posts.cpp
    core/events/message_pipeline.cpp
    core/events/midnight.cpp
    core/events/nickname_import.cpp
    core/events/nicknames.cpp
    core/events/reactions.cpp
    core/events/triggers.cpp
    core/events/embed_watch.cpp
    core/events/emoji_copies.cpp
    core/events/replacements.cpp
    core/events/url_replacer.cpp
    core/events/url_rules.cpp
    core/events/voice_sessions.cpp
    core/llm/advanced_triggers.cpp
    core/llm/aliases.cpp
    core/llm/anthropic.cpp
    core/llm/documents.cpp
    core/llm/guards.cpp
    core/llm/memory.cpp
    core/llm/memory_tools.cpp
    core/llm/models.cpp
    core/llm/openai.cpp
    core/llm/prompt.cpp
    core/llm/responder.cpp
    core/llm/settings.cpp
    core/llm/spend.cpp
    core/llm/stage.cpp
    core/llm/tools.cpp
    core/music/cookies.cpp
    core/music/links.cpp
    core/music/music_player.cpp
    core/music/music_queue.cpp
    core/music/pot_provider.cpp
    core/music/yt_dlp.cpp
    core/ports/clock.cpp
    core/ui/interaction.cpp
    core/ui/paginator.cpp
    core/util/ca_certificates.cpp
    core/util/env.cpp
    core/util/log.cpp
    core/util/match.cpp
    core/util/process.cpp
    core/util/text.cpp
    core/util/url_scan.cpp
)

# Headers are included as "core/<area>/<file>.hpp".
target_include_directories(latibot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(latibot_core PUBLIC dpp SQLite::SQLite3 ctre::ctre)
# DECtalk (cmake/dectalk.cmake). Its header stays out of ours: only
# core/audio/dectalk_engine.cpp includes it.
target_link_libraries(latibot_core PRIVATE dectalk)

# SHA-256 of emoji images, so one image is copied once
# (core/events/emoji_copies.cpp). DPP links OpenSSL already; this says so.
target_link_libraries(latibot_core PRIVATE OpenSSL::Crypto)

# crypt32 is the Windows certificate store, read at startup to give OpenSSL
# the roots it does not ship with (core/util/ca_certificates.cpp).
if(WIN32)
    target_link_libraries(latibot_core PRIVATE crypt32)
endif()

# ws2_32 resolves a music link's host, to refuse one into a private network
# (core/music/links.cpp).
if(WIN32)
    target_link_libraries(latibot_core PRIVATE ws2_32)
endif()

latibot_target_warnings(latibot_core)
latibot_target_sanitizers(latibot_core)

add_executable(LatiBot main.cpp)
target_link_libraries(LatiBot PRIVATE latibot_core)

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
set(LATIBOT_DEPLOY_SOURCES
    "${PROJECT_SOURCE_DIR}/deploy/Install-Dependencies.ps1"
    "${PROJECT_SOURCE_DIR}/deploy/Install-Dependencies.md"
)
add_custom_command(
    OUTPUT "${LATIBOT_DEPLOY_DIR}/Install-Dependencies.ps1" "${LATIBOT_DEPLOY_DIR}/Install-Dependencies.md"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LATIBOT_DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LATIBOT_DEPLOY_SOURCES} "${LATIBOT_DEPLOY_DIR}"
    DEPENDS ${LATIBOT_DEPLOY_SOURCES}
    COMMENT "Copying Install-Dependencies.ps1 beside LatiBot.exe"
    VERBATIM
)
add_custom_target(latibot_deploy_files ALL
    DEPENDS "${LATIBOT_DEPLOY_DIR}/Install-Dependencies.ps1" "${LATIBOT_DEPLOY_DIR}/Install-Dependencies.md"
)
add_dependencies(LatiBot latibot_deploy_files)
