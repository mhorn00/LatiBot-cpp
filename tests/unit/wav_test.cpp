// WAV files and voice-message waveforms (plan §12.8).

#include "core/audio/wav.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using latibot::audio::wav_file;
using latibot::audio::waveform;
using latibot::audio::waveform_base64;

TEST_CASE("a WAV file has the RIFF header, then the samples little-endian", "[audio]") {
    const std::vector<std::int16_t> samples{0x0102, -2};
    const std::string file = wav_file(samples, 11025, 1);

    // Every byte, since a player that rejects the header plays nothing.
    const std::string expected{
        "RIFF"
        "\x28\x00\x00\x00" // 36 + 4 bytes of data
        "WAVE"
        "fmt "
        "\x10\x00\x00\x00" // fmt chunk size
        "\x01\x00"         // PCM
        "\x01\x00"         // mono
        "\x11\x2B\x00\x00" // 11025 Hz
        "\x22\x56\x00\x00" // 22050 bytes a second
        "\x02\x00"         // 2 bytes a frame
        "\x10\x00"         // 16 bits
        "data"
        "\x04\x00\x00\x00"
        "\x02\x01"  // 0x0102
        "\xFE\xFF", // -2
        48};
    CHECK(file == expected);
}

TEST_CASE("a stereo WAV file counts both channels in its rates", "[audio]") {
    const std::string file = wav_file(std::vector<std::int16_t>(4, 0), 48000, 2);

    REQUIRE(file.size() == 44 + 8);
    // Bytes a second: 48000 * 2 channels * 2 bytes = 192000 = 0x0002EE00.
    CHECK(file.substr(28, 4) == std::string("\x00\xEE\x02\x00", 4));
    CHECK(file.substr(32, 2) == std::string("\x04\x00", 2)); // 4 bytes a frame
}

TEST_CASE("the waveform of silence is flat", "[audio]") {
    const auto shape = waveform(std::vector<std::int16_t>(10000, 0));

    REQUIRE(shape.size() == 256);
    CHECK(shape == std::vector<std::uint8_t>(256, 0));
}

TEST_CASE("the waveform follows where the sound is", "[audio]") {
    // Silence, then a loud stretch, then a quiet one: the Java version would
    // have drawn this as flat noise.
    std::vector<std::int16_t> samples(2560, 0);
    for (std::size_t i = 1024; i < 2048; ++i) {
        samples[i] = (i % 2 == 0) ? 20000 : -20000;
    }
    for (std::size_t i = 2048; i < 2560; ++i) {
        samples[i] = (i % 2 == 0) ? 5000 : -5000;
    }

    const auto shape = waveform(samples);

    REQUIRE(shape.size() == 256);
    CHECK(shape[0] == 0);
    CHECK(shape[101] == 0);
    CHECK(shape[103] == 255);
    CHECK(shape[203] == 255);
    CHECK(shape[210] == 63); // 5000 of 20000
    CHECK(shape[255] == 63);
}

TEST_CASE("a waveform of fewer samples than bars has one bar per sample", "[audio]") {
    CHECK(waveform(std::vector<std::int16_t>{0, 100, -200}) == std::vector<std::uint8_t>{0, 127, 255});
    CHECK(waveform({}).empty());
}

TEST_CASE("the waveform is sent as base64 of its 256 bytes", "[audio]") {
    const std::string encoded = waveform_base64(std::vector<std::int16_t>(10000, 0));

    // 256 bytes encode to 344 characters, padding included, and all-zero
    // bytes encode to 'A's.
    REQUIRE(encoded.size() == 344);
    CHECK(encoded.find_first_not_of("A=") == std::string::npos);
}
