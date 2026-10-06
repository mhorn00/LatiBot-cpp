# The dectalk module (README.md). DECtalk itself (cmake/dectalk.cmake) is only
# built with it; only dectalk_engine.cpp includes its header.
latibot_module(dectalk
    REQUIRES voice
    SOURCES
        src/chat_command.cpp
        src/dectalk_engine.cpp
        src/dectalk_sanitizer.cpp
        src/dectalk_speech.cpp
        src/module.cpp
        src/speak_command.cpp
        src/speech_queue.cpp
        src/voice_lab.cpp
        src/voice_params.cpp
        src/voice_store.cpp
        src/wav.cpp
    LINKS dectalk
    TESTS
        tests/chat_command_test.cpp
        tests/custom_voice_test.cpp
        tests/dectalk_engine_test.cpp
        tests/dectalk_golden_test.cpp
        tests/dectalk_module_test.cpp
        tests/dectalk_sanitizer_test.cpp
        tests/dectalk_speech_test.cpp
        tests/mock_tts_test.cpp
        tests/speak_command_test.cpp
        tests/speech_queue_test.cpp
        tests/voice_lab_panel_test.cpp
        tests/voice_lab_test.cpp
        tests/voice_params_test.cpp
        tests/voice_store_test.cpp
        tests/wav_test.cpp
)
