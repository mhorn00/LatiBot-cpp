add_library(latibot_core STATIC
    core/version.cpp
    core/audio/dectalk_engine.cpp
    core/audio/dectalk_sanitizer.cpp
    core/audio/pcm.cpp
    core/audio/speech_queue.cpp
    core/audio/voice_params.cpp
    core/audio/voice_store.cpp
    core/audio/wav.cpp
    core/bot.cpp
    core/config/bootstrap.cpp
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
    core/commands/nickname.cpp
    core/commands/trigger.cpp
    core/commands/urlrepl.cpp
    core/commands/preflight.cpp
    core/commands/registry.cpp
    core/commands/speak.cpp
    core/commands/voice.cpp
    core/commands/voice_lab.cpp
    core/discord/dpp_gateway.cpp
    core/discord/dpp_http_client.cpp
    core/discord/dpp_voice_output.cpp
    core/discord/message_flags.cpp
    core/discord/raw_api.cpp
    core/discord/voice_state.cpp
    core/events/backfill.cpp
    core/events/bot_allowlist.cpp
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
    core/ports/clock.cpp
    core/ui/interaction.cpp
    core/ui/paginator.cpp
    core/util/ca_certificates.cpp
    core/util/env.cpp
    core/util/log.cpp
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

latibot_target_warnings(latibot_core)
latibot_target_sanitizers(latibot_core)

add_executable(LatiBot main.cpp)
target_link_libraries(LatiBot PRIVATE latibot_core)

latibot_target_warnings(LatiBot)
latibot_target_sanitizers(LatiBot)
latibot_copy_runtime_dlls(LatiBot)
