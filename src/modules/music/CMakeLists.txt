# The music module (README.md).
latibot_module(music CONFIG
    REQUIRES voice
    SOURCES
        src/cookies.cpp
        src/links.cpp
        src/module.cpp
        src/music_command.cpp
        src/music_player.cpp
        src/music_queue.cpp
        src/pot_provider.cpp
        src/process.cpp
        src/yt_dlp.cpp
    # ws2_32 resolves a music link's host, to refuse one into a private
    # network (links.cpp).
    LINKS ws2_32
    TESTS
        tests/music_command_test.cpp
        tests/music_cookies_test.cpp
        tests/music_links_test.cpp
        tests/music_module_test.cpp
        tests/music_player_test.cpp
        tests/music_queue_test.cpp
        tests/pot_provider_test.cpp
        tests/process_test.cpp
        tests/yt_dlp_live_test.cpp
        tests/yt_dlp_test.cpp
)
