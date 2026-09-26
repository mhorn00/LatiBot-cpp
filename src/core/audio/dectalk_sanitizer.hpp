#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace latibot::audio {

/// Whose text is about to be spoken (plan §12.5).
enum class speech_trust : std::uint8_t {
    /// Anyone.
    user,
    /// A user in `trusted_users`, or an Administrator in a `trusted_guilds`
    /// server (plan §2.4).
    trusted,
    /// A language model. Never trusted, whoever asked it.
    llm,
};

/// Text made safe to hand to DECtalk, and what was taken out of it.
struct sanitized_speech {
    std::string text;

    /// The full name of each inline command removed, in order, for the log.
    /// Nothing is said to the speaker: stripping is silent.
    std::vector<std::string> removed;
};

/// Removes the inline commands `trust` may not use, and keeps the rest
/// (plan §12.5):
///
/// | command                          | user | trusted | llm |
/// |----------------------------------|------|---------|-----|
/// | play, log, debug, loadv, setv    | no   | yes     | no  |
/// | pause, resume                    | no   | no      | no  |
/// | the `save` in `[:dv ... save]`   | no   | no      | no  |
/// | everything else                  | yes  | yes     | yes |
///
/// `[:pause]` and `[:resume]` pause the audio device, which a memory engine
/// does not have: all they do is hold the engine up (plan §21.16).
///
/// Command names are matched as DECtalk matches them: case-insensitively,
/// as soon as a prefix is unique, so `[:PLA "x"]` is `[:play "x"]`, and
/// several commands may share a bracket, as in `[:rate 200 :play "x"]`.
///
/// Rather than editing the text in place, the result is rebuilt: plain text
/// with no `[` in it, phoneme brackets with no `[` inside, and each kept
/// command written out by this function as `[:name parameters]`, with
/// parameters limited to characters that cannot open or close anything.
/// DECtalk only ever sees commands written here, so a bracket this function
/// reads differently from DECtalk can cost some text but cannot smuggle a
/// command through. Control characters are dropped.
[[nodiscard]] auto sanitize_speech(std::string_view text, speech_trust trust) -> sanitized_speech;

} // namespace latibot::audio
