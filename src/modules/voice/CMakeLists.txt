# The voice module (README.md). Built when dectalk or music is, which require
# it: it has no switch of its own (docs/modules/Module_Plan_Final.md D2).
latibot_module(voice
    SOURCES
        src/dpp_voice_output.cpp
        src/join_command.cpp
        src/module.cpp
        src/pcm.cpp
        src/voice_command.cpp
        src/voice_mixer.cpp
        src/voice_sessions.cpp
        src/voice_state.cpp
    TESTS
        tests/join_command_test.cpp
        tests/pcm_test.cpp
        tests/voice_command_test.cpp
        tests/voice_mixer_test.cpp
        tests/voice_module_test.cpp
        tests/voice_sessions_test.cpp
)
