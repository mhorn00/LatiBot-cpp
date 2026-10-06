#include "voice_lab.hpp"

#include "core/commands/options.hpp"
#include "core/ports/clock.hpp"
#include "core/ui/interaction.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"
#include "speech_queue.hpp"
#include "tts_engine.hpp"
#include "voice/pcm.hpp"
#include "voice/voice_state.hpp"
#include "voice_store.hpp"

#include <dpp/cluster.h>
#include <dpp/discordclient.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <vector>

namespace latibot::commands {
namespace {

/// How much of the lab's notes the panel shows; the rest of a long list of
/// problems is not worth the space.
constexpr std::size_t note_limit = 400;

/// The longest `[:dv]` text the raw form takes.
constexpr std::size_t raw_limit = 1000;

auto button(dpp::component_style style, std::string_view label, std::string_view view) -> dpp::component {
    return dpp::component()
        .set_type(dpp::cot_button)
        .set_style(style)
        .set_label(std::string(label))
        .set_id(ui::encode({.view = std::string(view), .page = 0, .argument = {}}).value_or(std::string(view)));
}

auto row_of(const dpp::component& item) -> dpp::component {
    dpp::component row;
    row.set_type(dpp::cot_action_row).add_component(item);
    return row;
}

/// "[:nh][:dv ap 200]": the whole voice as inline commands.
auto as_commands(const audio::custom_voice& voice) -> std::string {
    const audio::builtin_voice* base = audio::find_builtin_voice(voice.base);
    std::string written(base == nullptr ? "[:np]" : base->command);
    const std::string edits = voice.dv_parameters();
    if (!edits.empty()) written += std::format("[:dv {}]", edits);
    return written;
}

auto join(const std::vector<std::string>& parts) -> std::string {
    std::string joined;
    for (const std::string& part : parts) {
        if (!joined.empty()) joined += "; ";
        joined += part;
    }
    return joined;
}

} // namespace

// --------------------------------------------------------------------------
// Drafts
// --------------------------------------------------------------------------

voice_drafts::voice_drafts(ports::clock& clock, std::chrono::minutes keep) : clock_(&clock), keep_(keep) {}

auto voice_drafts::get(dpp::snowflake guild, dpp::snowflake user) -> voice_draft {
    const std::scoped_lock lock(mutex_);
    const auto now = clock_->steady_now();
    std::erase_if(drafts_, [&](const auto& entry) { return now - entry.second.touched >= keep_; });

    const auto found = drafts_.find({guild, user});
    return found == drafts_.end() ? voice_draft{} : found->second.draft;
}

auto voice_drafts::put(dpp::snowflake guild, dpp::snowflake user, voice_draft draft) -> void {
    const std::scoped_lock lock(mutex_);
    drafts_.insert_or_assign({guild, user}, kept{.draft = std::move(draft), .touched = clock_->steady_now()});
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

namespace {

/// The edits, a line per group that has any, then what the rest is.
auto describe_edits(const audio::custom_voice& voice, std::string_view base_name) -> std::string {
    std::string text;
    for (const audio::voice_parameter_group& group : audio::voice_parameter_groups()) {
        std::string line;
        for (const std::string_view code : group.codes) {
            const auto value = code.empty() ? std::nullopt : voice.get(code);
            if (!value) continue;
            if (!line.empty()) line += " · ";
            line += std::format("{} {}", code, *value);
        }
        if (!line.empty()) text += std::format("**{}** {}\n", group.name, line);
    }
    text += text.empty() ? std::format("No changes yet: this is {} as DECtalk has them.\n", base_name)
                         : std::format("Everything else is {}'s own.\n", base_name);
    return text;
}

/// The menu that opens a group's form.
auto group_menu() -> dpp::component {
    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("Change a group of settings")
        .set_id(ui::encode({.view = std::string(lab_edit_view), .page = 0, .argument = {}}).value_or(std::string(lab_edit_view)));

    const auto groups = audio::voice_parameter_groups();
    for (std::size_t index = 0; index < groups.size(); ++index) {
        std::string codes;
        for (const std::string_view code : groups[index].codes) {
            if (code.empty()) continue;
            if (!codes.empty()) codes += ", ";
            codes += code;
        }
        menu.add_select_option(dpp::select_option(std::string(groups[index].name), std::to_string(index), codes));
    }
    menu.add_select_option(dpp::select_option("As [:dv] text", std::string(lab_raw_form), "Copy the whole voice, or paste one"));
    return menu;
}

/// The menu that opens one of the server's saved voices into the draft.
/// Nothing when there are none. The voice being edited is always offered,
/// so it can be picked again to throw away changes to it.
auto saved_menu(std::span<const audio::saved_voice> saved, std::string_view editing) -> std::optional<dpp::component> {
    if (saved.empty()) return std::nullopt;

    std::vector<const audio::saved_voice*> offered;
    const auto current = std::ranges::find(saved, editing, &audio::saved_voice::name);
    if (current != saved.end()) offered.push_back(&*current);
    for (const audio::saved_voice& voice : saved) {
        if (offered.size() == saved_voices_offered) break;
        if (&voice != (current == saved.end() ? nullptr : &*current)) offered.push_back(&voice);
    }
    std::ranges::sort(offered, {}, &audio::saved_voice::name);

    const std::string placeholder =
        saved.size() > offered.size()
            ? std::format("Open a saved voice ({} of {}; /tts voices lab voice: opens any)", offered.size(), saved.size())
            : std::string("Open a saved voice");

    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu)
        .set_placeholder(placeholder)
        .set_id(ui::encode({.view = std::string(lab_open_view), .page = 0, .argument = {}}).value_or(std::string(lab_open_view)));
    for (const audio::saved_voice* voice : offered) {
        menu.add_select_option(dpp::select_option(voice->name, voice->name, util::truncate(as_commands(voice->voice), 100))
                                   .set_default(voice->name == editing));
    }
    return menu;
}

/// What the panel is editing, in a line: a new voice, or a saved one and
/// whether the draft still matches it.
auto describe_editing(const voice_draft& draft, std::span<const audio::saved_voice> saved) -> std::string {
    if (draft.name.empty()) return "a new voice, not saved yet";
    const auto found = std::ranges::find(saved, draft.name, &audio::saved_voice::name);
    if (found == saved.end()) return std::format("`{}`, which is no longer saved; **Save as…** keeps it again", draft.name);
    if (found->voice == draft.voice) return std::format("`{}`, as saved", draft.name);
    return std::format("`{}`, with **unsaved changes**", draft.name);
}

/// The menu that picks the built-in voice underneath.
auto base_menu(std::string_view base_name) -> dpp::component {
    dpp::component menu;
    menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("Built on")
        .set_id(ui::encode({.view = std::string(lab_base_view), .page = 0, .argument = {}}).value_or(std::string(lab_base_view)));
    for (const audio::builtin_voice& voice : audio::builtin_voices()) {
        menu.add_select_option(dpp::select_option(std::string(voice.name), std::string(voice.name), std::string(voice.description))
                                   .set_default(voice.name == base_name));
    }
    return menu;
}

} // namespace

auto render_voice_lab(const voice_draft& draft, std::span<const audio::saved_voice> saved) -> dpp::message {
    const audio::builtin_voice* base = audio::find_builtin_voice(draft.voice.base);
    const std::string_view base_name = base == nullptr ? std::string_view{"paul"} : base->name;
    const std::string_view description = base == nullptr ? std::string_view{} : base->description;

    std::string content = std::format("**Voice lab**: editing {}\n", describe_editing(draft, saved));
    content += std::format("Built on **{}** ({})\n", base_name, description);
    content += describe_edits(draft.voice, base_name);
    content += std::format("`{}`", as_commands(draft.voice));
    if (!draft.note.empty()) content += std::format("\n-# {}", util::truncate(draft.note, note_limit));

    dpp::message panel(content);
    panel.set_allowed_mentions();
    if (const auto menu = saved_menu(saved, draft.name)) panel.add_component(row_of(*menu));
    panel.add_component(row_of(group_menu()));
    panel.add_component(row_of(base_menu(base_name)));

    dpp::component controls;
    controls.set_type(dpp::cot_action_row);
    controls.add_component(button(dpp::cos_primary, "▶ Test", lab_test_view));
    controls.add_component(button(dpp::cos_success, draft.name.empty() ? "Save as…" : "Save…", lab_save_view));
    controls.add_component(button(dpp::cos_secondary, "New voice", lab_reset_view));
    panel.add_component(controls);
    return panel;
}

auto voice_lab_form(std::string_view which, const voice_draft& draft) -> std::optional<dpp::interaction_modal_response> {
    const std::string id =
        ui::encode({.view = std::string(lab_form_view), .page = 0, .argument = std::string(which)}).value_or(std::string(lab_form_view));

    if (which == lab_raw_form) {
        dpp::interaction_modal_response form(id, "The voice as [:dv] text");
        form.add_component(dpp::component()
                               .set_label("Inline commands")
                               .set_placeholder("[:nh][:dv ap 200 pr 150]")
                               .set_id(std::string(lab_raw_form))
                               .set_type(dpp::cot_text)
                               .set_text_style(dpp::text_paragraph)
                               .set_required(false)
                               .set_max_length(raw_limit)
                               .set_default_value(as_commands(draft.voice)));
        return form;
    }

    std::size_t index = 0;
    const auto [end, error] = std::from_chars(which.data(), which.data() + which.size(), index);
    const auto groups = audio::voice_parameter_groups();
    if (error != std::errc{} || end != which.data() + which.size() || index >= groups.size()) return std::nullopt;

    dpp::interaction_modal_response form(id, std::string(groups[index].name));
    bool first = true;
    for (const std::string_view code : groups[index].codes) {
        const audio::voice_parameter* parameter = audio::find_voice_parameter(code);
        if (parameter == nullptr) continue;
        if (!first) form.add_row();
        first = false;

        const auto current = draft.voice.get(code);
        form.add_component(
            dpp::component()
                .set_label(std::string(parameter->label))
                .set_placeholder(std::format("{} to {}; empty keeps {}'s own", parameter->min, parameter->max, draft.voice.base))
                .set_id(std::string(code))
                .set_type(dpp::cot_text)
                .set_text_style(dpp::text_short)
                .set_required(false)
                .set_max_length(6)
                .set_default_value(current ? std::to_string(*current) : ""));
    }
    return form;
}

auto voice_name_form(const voice_draft& draft) -> dpp::interaction_modal_response {
    dpp::interaction_modal_response form(
        ui::encode({.view = std::string(lab_name_view), .page = 0, .argument = {}}).value_or(std::string(lab_name_view)), "Save the voice");
    form.add_component(dpp::component()
                           .set_label("Name")
                           .set_placeholder("letters, digits, - and _")
                           .set_id("name")
                           .set_type(dpp::cot_text)
                           .set_text_style(dpp::text_short)
                           .set_required(true)
                           .set_min_length(1)
                           .set_max_length(32)
                           .set_default_value(draft.name));
    return form;
}

auto apply_voice_form(voice_draft& draft, std::string_view which, const std::map<std::string, std::string, std::less<>>& fields) -> void {
    std::vector<std::string> problems;

    if (which == lab_raw_form) {
        // Missing is not the same as emptied: reading nothing as "no edits"
        // would wipe the voice.
        const auto found = fields.find(lab_raw_form);
        if (found == fields.end()) {
            draft.note = "that form came back empty, so nothing changed";
            return;
        }
        const audio::parsed_voice parsed = audio::parse_custom_voice(found->second, draft.voice.base);
        draft.voice = parsed.voice;
        draft.note = join(parsed.problems);
        return;
    }

    for (const auto& [code, text] : fields) {
        const audio::voice_parameter* parameter = audio::find_voice_parameter(code);
        if (parameter == nullptr) continue;

        const std::string_view value_text = util::trim(text);
        if (value_text.empty()) {
            draft.voice.clear(parameter->code);
            continue;
        }
        int value = 0;
        const auto [end, error] = std::from_chars(value_text.data(), value_text.data() + value_text.size(), value);
        if (error != std::errc{} || end != value_text.data() + value_text.size()) {
            problems.push_back(std::format("{} needs a number, not \"{}\"", parameter->code, value_text));
            continue;
        }
        const int kept = std::clamp(value, parameter->min, parameter->max);
        if (kept != value) {
            problems.push_back(
                std::format("{} goes from {} to {}, so {} became {}", parameter->code, parameter->min, parameter->max, value, kept));
        }
        draft.voice.set(parameter->code, value);
    }
    draft.note = join(problems);
}

auto voice_change_refusal(dpp::snowflake user, dpp::snowflake created_by, bool administrator, std::string_view name)
    -> std::optional<std::string> {
    if (user == created_by || administrator) return std::nullopt;
    return std::format("only whoever made `{}`, or an admin, can change it", name);
}

// --------------------------------------------------------------------------
// The panel's buttons and forms
// --------------------------------------------------------------------------

voice_lab::voice_lab(voice_drafts& drafts, audio::voice_store& store, ports::clock& clock, speech_services speech)
    : drafts_(&drafts), store_(&store), clock_(&clock), speech_(speech) {}

auto voice_lab::open(dpp::snowflake guild, dpp::snowflake user, std::string_view from) -> std::optional<dpp::message> {
    voice_draft draft = drafts_->get(guild, user);
    if (!util::is_blank(from)) {
        const auto saved = store_->find(guild, from);
        if (!saved) return std::nullopt;
        draft = voice_draft{.voice = saved->voice, .name = saved->name, .note = {}};
    }
    draft.note.clear();
    drafts_->put(guild, user, draft);
    return render_voice_lab(draft, store_->list(guild));
}

auto voice_lab::on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake user = event.command.get_issuing_user().id;
    voice_draft draft = drafts_->get(guild, user);
    draft.note.clear();

    if (state.view == lab_edit_view) {
        if (auto form = voice_lab_form(chosen, draft)) {
            event.dialog(*form);
        } else {
            ui::update_panel(event, render_voice_lab(draft, store_->list(guild)));
        }
        return true;
    }
    if (state.view == lab_save_view) {
        event.dialog(voice_name_form(draft));
        return true;
    }

    if (state.view == lab_open_view) {
        // Opening one replaces the draft, unsaved changes and all, which is
        // also how changes to a saved voice are thrown away.
        if (const auto saved = store_->find(guild, chosen)) {
            draft = voice_draft{.voice = saved->voice, .name = saved->name, .note = std::format("opened `{}`", saved->name)};
        } else {
            draft.note = std::format("`{}` is no longer saved here", chosen);
        }
    } else if (state.view == lab_base_view) {
        if (audio::find_builtin_voice(chosen) != nullptr) draft.voice.base = chosen;
    } else if (state.view == lab_reset_view) {
        draft = voice_draft{};
    } else if (state.view == lab_test_view) {
        const discord::voice_plan plan =
            discord::plan_voice(discord::bot_voice_channel(event.from(), guild), discord::voice_channel_of(guild, user));
        if (plan.route == discord::voice_route::nowhere) {
            draft.note = "i'm not in a voice channel, and neither are you";
        } else {
            if (plan.route == discord::voice_route::join_caller && event.from() != nullptr) {
                event.from()->connect_voice(guild, plan.channel);
            }
            draft.note = "saying the test phrase";
            ui::detach(test(guild, user, draft.voice), "the voice lab's test");
        }
    } else {
        return false;
    }

    drafts_->put(guild, user, draft);
    ui::update_panel(event, render_voice_lab(draft, store_->list(guild)));
    return true;
}

auto voice_lab::on_form(const dpp::form_submit_t& event, const ui::page_state& state) -> bool {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake user = event.command.get_issuing_user().id;
    voice_draft draft = drafts_->get(guild, user);

    const ui::form_values fields = ui::form_fields(event);
    if (state.view == lab_form_view) {
        apply_voice_form(draft, state.argument, fields);
    } else if (state.view == lab_name_view) {
        const auto name = fields.find("name");
        save(event, draft, name == fields.end() ? std::string_view{} : std::string_view(name->second));
    } else {
        return false;
    }

    drafts_->put(guild, user, draft);
    ui::update_panel(event, render_voice_lab(draft, store_->list(guild)));
    return true;
}

auto voice_lab::save(const dpp::form_submit_t& event, voice_draft& draft, std::string_view name) -> void {
    const dpp::snowflake guild = event.command.guild_id;
    const dpp::snowflake user = event.command.get_issuing_user().id;

    if (const auto refused = audio::voice_name_refusal(name)) {
        draft.note = *refused;
        return;
    }
    const std::string normal = audio::normalise_voice_name(name);

    const auto existing = store_->find(guild, normal);
    const bool administrator = invoker_permissions(event).can(dpp::p_administrator);
    if (existing) {
        if (const auto refused = voice_change_refusal(user, existing->created_by, administrator, normal)) {
            draft.note = *refused;
            return;
        }
    } else if (store_->count(guild) >= audio::max_saved_voices) {
        draft.note = std::format("this server already keeps {} voices, the most it can; delete one first", audio::max_saved_voices);
        return;
    }

    const auto now = std::chrono::time_point_cast<std::chrono::seconds>(clock_->now());
    store_->save(guild, {.name = normal, .voice = draft.voice, .created_by = existing ? existing->created_by : user, .updated_at = now});
    util::log().info("voice {} {} in guild {} by {}: {}", normal, existing ? "updated" : "saved", guild,
                     describe_user(event.command.get_issuing_user()), as_commands(draft.voice));
    draft.name = normal;
    draft.note = std::format("saved as `{}`; /speak voice:{} uses it", normal, normal);
}

auto voice_lab::test(dpp::snowflake guild, dpp::snowflake user, audio::custom_voice voice) const -> dpp::task<void> {
    const speech_limits limits = speech_limits_for(*speech_.settings, guild);
    const std::uint64_t ticket = speech_.queue->ticket(guild);

    auto spoken = co_await speech_.engine->synthesize(
        {.text = std::string(voice_test_phrase),
         .voice = {.voice = voice.base, .rate = audio::default_rate, .volume = 100, .custom_params = voice.dv_parameters()},
         .max_duration = limits.max_duration});
    if (!spoken.has_value()) {
        util::log().warn("the voice lab's test in guild {} failed: {}", guild, spoken.error().message);
        co_return;
    }
    const ports::pcm_audio& pcm = spoken.value();
    speech_.queue->enqueue(guild, user, ticket, audio::to_discord(pcm.samples, pcm.sample_rate));
}

} // namespace latibot::commands
