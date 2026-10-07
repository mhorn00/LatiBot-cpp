#pragma once

#include "core/ui/paginator.hpp"
#include "speak_command.hpp"
#include "voice_params.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace latibot::ports {
class clock;
}

namespace latibot::audio {
class voice_store;
struct saved_voice;
} // namespace latibot::audio

namespace latibot::commands {

// The voice lab's views (src/modules/dectalk/docs/Speech.md §3). All of them act on the
// draft of whoever pressed them, so none needs an argument but the forms,
// which say which group they edit.
inline constexpr std::string_view lab_edit_view = "vlabedit";
inline constexpr std::string_view lab_base_view = "vlabbase";
inline constexpr std::string_view lab_test_view = "vlabtest";
inline constexpr std::string_view lab_save_view = "vlabsave";
inline constexpr std::string_view lab_reset_view = "vlabreset";
inline constexpr std::string_view lab_form_view = "vlabform";
inline constexpr std::string_view lab_name_view = "vlabname";

/// The menu that opens one of the server's saved voices.
inline constexpr std::string_view lab_open_view = "vlabopen";

/// How many saved voices that menu lists: Discord's limit on a menu's
/// options. A server can keep more, which `/tts voices lab voice:` opens.
inline constexpr std::size_t saved_voices_offered = 25;

/// The form that edits the whole voice as `[:dv]` text, rather than a group.
inline constexpr std::string_view lab_raw_form = "raw";

/// What the lab's ▶ Test says.
inline constexpr std::string_view voice_test_phrase = "Hello! This is how I sound now. What do you think?";

/// A voice being worked on in the lab.
struct voice_draft {
    audio::custom_voice voice;

    /// The saved voice it was opened from or last saved as, which Save as
    /// offers.
    std::string name;

    /// What the lab last has to say: what a form could not read, what was
    /// saved. Shown once, under the voice.
    std::string note;
};

/// Each person's draft in each guild, kept for a while after it was last
/// touched, so closing the panel by accident does not lose it. Thread-safe.
class voice_drafts {
public:
    explicit voice_drafts(ports::clock& clock, std::chrono::minutes keep = std::chrono::minutes{30});

    /// Their draft, or a new one of Paul's when there is none or it expired.
    [[nodiscard]] auto get(dpp::snowflake guild, dpp::snowflake user) -> voice_draft;

    auto put(dpp::snowflake guild, dpp::snowflake user, voice_draft draft) -> void;

private:
    struct kept {
        voice_draft draft;
        std::chrono::steady_clock::time_point touched;
    };

    ports::clock* clock_;
    std::chrono::minutes keep_;

    std::mutex mutex_;
    std::map<std::pair<dpp::snowflake, dpp::snowflake>, kept> drafts_;
};

/// The panel: which voice is being edited and whether it still matches what
/// is saved, the voice as it stands, then the controls. `saved` is the
/// server's saved voices, for the menu that opens one.
[[nodiscard]] auto render_voice_lab(const voice_draft& draft, std::span<const audio::saved_voice> saved = {}) -> dpp::message;

/// The form for one group of parameters, by its index in
/// `voice_parameter_groups`, or `lab_raw_form` for the whole voice as text.
/// Nothing for anything else.
[[nodiscard]] auto voice_lab_form(std::string_view which, const voice_draft& draft) -> std::optional<dpp::interaction_modal_response>;

/// The Save as form.
[[nodiscard]] auto voice_name_form(const voice_draft& draft) -> dpp::interaction_modal_response;

/// Applies what a group's form or the raw form sent, by field id. A field
/// left empty goes back to the base voice's own value. Anything that could
/// not be used is said in `draft.note`.
auto apply_voice_form(voice_draft& draft, std::string_view which, const std::map<std::string, std::string, std::less<>>& fields) -> void;

/// Why `user` may not replace the saved voice made by `created_by`, or
/// nothing when they may: whoever made it, or an administrator.
[[nodiscard]] auto voice_change_refusal(dpp::snowflake user, dpp::snowflake created_by, bool administrator, std::string_view name)
    -> std::optional<std::string>;

/// The lab's buttons and forms.
///
/// It routes its own, unlike the older panels whose routing lives in the
/// shell: its Test button has to synthesize and speak, which is this
/// module's business rather than the shell's.
class voice_lab {
public:
    voice_lab(voice_drafts& drafts, audio::voice_store& store, ports::clock& clock, speech_services speech);

    /// The panel for `/tts voices lab`, starting from the saved voice `from`, or
    /// from the person's draft when `from` is empty. Nothing when there is
    /// no saved voice of that name.
    [[nodiscard]] auto open(dpp::snowflake guild, dpp::snowflake user, std::string_view from) -> std::optional<dpp::message>;

    /// False when the view is not one of the lab's.
    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// False when the form is not one of the lab's.
    auto on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool;

private:
    /// Speaks the test phrase in the draft's voice. The panel has already
    /// been answered.
    auto test(dpp::snowflake guild, dpp::snowflake user, audio::custom_voice voice) const -> dpp::task<void>;

    auto save(const dpp::form_submit_t& event, voice_draft& draft, std::string_view name) -> void;

    voice_drafts* drafts_;
    audio::voice_store* store_;
    ports::clock* clock_;
    speech_services speech_;
};

} // namespace latibot::commands
