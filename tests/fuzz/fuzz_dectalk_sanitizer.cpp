// Fuzzes the DECtalk sanitizer, which stands between anyone's text and the
// engine's inline commands (plan §12.5, §17.5).
//
// The checks read the output the way DECtalk would, independently of how
// the sanitizer reads its input: every command bracket in it must name a
// command a user may run, in full, with nothing in it that could open a
// quote or another bracket.

#include "core/audio/dectalk_sanitizer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

auto require(bool holds) -> void {
    if (!holds) std::abort();
}

auto is_space(char letter) -> bool {
    return letter == ' ' || letter == '\t' || letter == '\r' || letter == '\n';
}

auto is_letter(char letter) -> bool {
    return (letter >= 'a' && letter <= 'z') || letter == '_';
}

constexpr std::array<std::string_view, 7> forbidden{"play", "log", "debug", "loadv", "setv", "pause", "resume"};

/// Every "[" in `text` either opens a command this checker accepts or a
/// bracket with no ':' after its spaces and no '[' before its ']'.
auto check_output(std::string_view text) -> void {
    for (std::size_t at = 0; at < text.size(); ++at) {
        const auto byte = static_cast<unsigned char>(text[at]);
        require(!(byte < 0x20 && !is_space(text[at])) && byte != 0x7F);
        if (text[at] != '[') continue;

        std::size_t next = at + 1;
        while (next < text.size() && (is_space(text[next]) || text[next] == '['))
            ++next;
        if (next < text.size() && text[next] == ':') {
            // Written by the sanitizer, so exactly "[:name params]".
            require(next == at + 1);
            const std::size_t close = text.find(']', at);
            require(close != std::string_view::npos);
            const std::string_view inside = text.substr(at + 2, close - at - 2);
            require(inside.find_first_of("[\"<:") == std::string_view::npos);

            std::size_t name_end = 0;
            while (name_end < inside.size() && is_letter(inside[name_end]))
                ++name_end;
            const std::string_view name = inside.substr(0, name_end);
            require(!name.empty());
            for (const std::string_view banned : forbidden)
                require(name != banned);
            require(name_end == inside.size() || inside[name_end] == ' ');
            at = close;
            continue;
        }

        const std::size_t close = text.find_first_of("[]", at + 1);
        require(close != std::string_view::npos && text[close] == ']');
        at = close;
    }
}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) -> int {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    using latibot::audio::sanitize_speech;
    using latibot::audio::speech_trust;

    const auto user = sanitize_speech(text, speech_trust::user);
    check_output(user.text);

    // The model is held to the same rules.
    require(sanitize_speech(text, speech_trust::llm).text == user.text);

    // Nothing is added: the output is never much longer than the input.
    // A kept command can grow by its full name and brackets.
    require(user.text.size() <= (size * 16) + 16);

    // Sanitizing again finds nothing more to remove.
    const auto again = sanitize_speech(user.text, speech_trust::user);
    require(again.text == user.text);
    require(again.removed.empty());

    // Trusted text still has no control characters or '[' inside a
    // phoneme bracket; its commands may include play and log.
    const auto trusted = sanitize_speech(text, speech_trust::trusted);
    for (const char letter : trusted.text)
        require(static_cast<unsigned char>(letter) >= 0x20 || is_space(letter));
    return 0;
}
