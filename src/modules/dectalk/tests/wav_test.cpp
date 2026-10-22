// WAV files, which /chat sends (src/modules/dectalk/docs/Speech.md §4.4).

#include "wav.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using latibot::audio::wav_file;

TEST_CASE("a WAV file has the RIFF header, then the samples little-endian", "[dectalk]") {
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

TEST_CASE("a stereo WAV file counts both channels in its rates", "[dectalk]") {
    const std::string file = wav_file(std::vector<std::int16_t>(4, 0), 48000, 2);

    REQUIRE(file.size() == 44 + 8);
    // Bytes a second: 48000 * 2 channels * 2 bytes = 192000 = 0x0002EE00.
    CHECK(file.substr(28, 4) == std::string("\x00\xEE\x02\x00", 4));
    CHECK(file.substr(32, 2) == std::string("\x04\x00", 2)); // 4 bytes a frame
}
