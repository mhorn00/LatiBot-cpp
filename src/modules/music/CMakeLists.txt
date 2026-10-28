# The music module (README.md).
#
# ws2_32 resolves a music link's host on Windows, to refuse one into a private
# network (src/links.cpp). Starting yt-dlp and ffmpeg is the core's
# (core/util/process.hpp).
if(WIN32)
    set(music_platform_links ws2_32)
else()
    set(music_platform_links "")
endif()

latibot_module(music CONFIG
    REQUIRES voice
    SOURCES
        src/cookies.cpp
        src/links.cpp
        src/module.cpp
        src/music_command.cpp
        src/music_tools.cpp
        src/music_player.cpp
        src/music_queue.cpp
        src/pot_provider.cpp
        src/yt_dlp.cpp
    LINKS ${music_platform_links}
    TESTS
        tests/music_command_test.cpp
        tests/music_cookies_test.cpp
        tests/music_links_test.cpp
        tests/music_module_test.cpp
        tests/music_tools_test.cpp
        tests/music_player_test.cpp
        tests/music_queue_test.cpp
        tests/pot_provider_test.cpp
        tests/yt_dlp_live_test.cpp
        tests/yt_dlp_test.cpp
)
