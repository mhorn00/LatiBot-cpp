# The soundboard module (README.md).
latibot_module(soundboard CONFIG
    SOURCES
        src/clips.cpp
        src/module.cpp
        src/soundboard_command.cpp
        src/sound_tools.cpp
        src/sounds.cpp
    TESTS
        tests/soundboard_clips_live_test.cpp
        tests/soundboard_clips_test.cpp
        tests/soundboard_command_test.cpp
        tests/soundboard_module_test.cpp
        tests/soundboard_sounds_test.cpp
        tests/sound_tools_test.cpp
)
