# The core (docs/modules/Module_Plan_Final.md §3.2, §6): the bot itself, and
# what modules build on. Its public headers are in include/core/, which every
# module sees; its private ones in src/, which only it, the executable and its
# tests see. A public header is included as "core/<area>/<file>.hpp", a private
# one as "<area>/<file>.hpp".
add_library(latibot_core STATIC
    src/bot.cpp
    src/commands/basic.cpp
    src/commands/bots.cpp
    src/commands/logs.cpp
    src/commands/message_options.cpp
    src/commands/options.cpp
    src/commands/preflight.cpp
    src/commands/registry.cpp
    src/commands/unregister.cpp
    src/config/bootstrap.cpp
    src/config/command_line.cpp
    src/config/guild_settings.cpp
    src/config/section.cpp
    src/db/backup.cpp
    src/db/database.cpp
    src/db/error.cpp
    src/db/migrations.cpp
    src/db/schema_versions.cpp
    src/db/schemas.cpp
    src/db/statement.cpp
    src/discord/dpp_gateway.cpp
    src/discord/dpp_http_client.cpp
    src/discord/dpp_log.cpp
    src/discord/message_flags.cpp
    src/discord/raw_api.cpp
    src/discord/unregister_commands.cpp
    src/events/bot_allowlist.cpp
    src/events/goodbye.cpp
    src/events/log_channel.cpp
    src/events/message_pipeline.cpp
    src/modules/capability_registry.cpp
    src/modules/host.cpp
    src/modules/module.cpp
    src/ports/clock.cpp
    src/ui/interaction.cpp
    src/ui/paginator.cpp
    src/ui/panel_routes.cpp
    src/util/ca_certificates.cpp
    src/util/env.cpp
    src/util/log.cpp
    src/util/match.cpp
    src/util/text.cpp
    src/util/url_scan.cpp
    src/version.cpp
)

target_include_directories(latibot_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(latibot_core PUBLIC dpp::dpp SQLite::SQLite3 ctre::ctre)
# crypt32 is the Windows certificate store, read at startup to give OpenSSL
# the roots it does not ship with (src/util/ca_certificates.cpp).
if(WIN32)
    target_link_libraries(latibot_core PRIVATE crypt32)
endif()

# DPP's headers, compiled once rather than in every file: a clean build of
# our code takes 72 s rather than 124 (docs/modules/Module_Plan_Final.md §9.1,
# I8). clang-tidy leaves it out, so every file still has to include what it
# uses (tools/Invoke-ClangTidy.ps1).
target_precompile_headers(latibot_core PRIVATE <dpp/dpp.h>)
latibot_target_warnings(latibot_core)
latibot_target_sanitizers(latibot_core)

