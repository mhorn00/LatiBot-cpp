add_library(latibot_core STATIC
    core/version.cpp
    core/bot.cpp
    core/config/bootstrap.cpp
    core/config/guild_settings.cpp
    core/db/backup.cpp
    core/db/database.cpp
    core/db/error.cpp
    core/db/migrations.cpp
    core/db/statement.cpp
    core/commands/basic.cpp
    core/commands/registry.cpp
    core/discord/dpp_gateway.cpp
    core/discord/dpp_http_client.cpp
    core/discord/raw_api.cpp
    core/ports/clock.cpp
    core/util/env.cpp
    core/util/log.cpp
    core/util/text.cpp
)

# Headers are included as "core/<area>/<file>.hpp".
target_include_directories(latibot_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(latibot_core PUBLIC dpp SQLite::SQLite3 ctre::ctre)

latibot_target_warnings(latibot_core)
latibot_target_sanitizers(latibot_core)

add_executable(LatiBot main.cpp)
target_link_libraries(LatiBot PRIVATE latibot_core)

latibot_target_warnings(LatiBot)
latibot_target_sanitizers(LatiBot)
latibot_copy_runtime_dlls(LatiBot)
