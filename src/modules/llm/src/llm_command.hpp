#pragma once

#include "core/commands/registry.hpp"
#include "core/ui/paginator.hpp"
#include "documents.hpp"
#include "llm_config.hpp"
#include "memory.hpp"
#include "models.hpp"
#include "settings.hpp"
#include "spend.hpp"

#include <dpp/appcommand.h>
#include <dpp/dispatcher.h>
#include <dpp/message.h>
#include <dpp/snowflake.h>

#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace latibot::config {
class guild_settings;
} // namespace latibot::config

namespace latibot::llm {
class advanced_trigger_store;
class alias_store;
class blacklist_store;
class document_store;
class memory_store;
class usage_store;
} // namespace latibot::llm

namespace latibot::ports {
class clock;
class http_client;
} // namespace latibot::ports

namespace latibot::commands {

// --------------------------------------------------------------------------
// Decisions and rendering
// --------------------------------------------------------------------------

/// Whether someone may edit the personality: anyone with Manage Server, or
/// anyone holding the guild's personality role. The role is @everyone, the
/// guild's own id, until an admin narrows it
/// (src/modules/llm/docs/Language_Model.md §3.5).
[[nodiscard]] auto may_edit_personality(bool manages_server, dpp::snowflake guild, dpp::snowflake editor_role,
                                        std::span<const dpp::snowflake> roles) -> bool;

/// Where spending stands, and what this guild's model is set to: `/llm status`.
struct llm_overview {
    bool enabled = false;
    std::string model;
    bool has_key = false;
    llm::spend_status spend;
    llm::spend_caps caps;
    double guild_this_month = 0;
};

[[nodiscard]] auto render_llm_status(const llm_overview& overview) -> std::string;

/// A document cut into the parts a form holds: at most five of 4000
/// characters each, split between lines where possible. Nothing when it
/// will not fit, which is when an attachment is the way to edit it.
[[nodiscard]] auto document_parts(std::string_view text) -> std::optional<std::vector<std::string>>;

/// The parts a form sent back, as one document again: the non-empty ones,
/// joined with the line break they were split at.
[[nodiscard]] auto join_document_parts(std::span<const std::string> parts) -> std::string;

/// The form that edits a document, pre-filled. Nothing when it is too long
/// for one.
[[nodiscard]] auto document_form(llm::document_kind kind, std::string_view current) -> std::optional<dpp::interaction_modal_response>;

/// What a document says, for `view`: in a code block when it fits in a
/// message, and as a file when it does not.
[[nodiscard]] auto render_document(llm::document_kind kind, const llm::document_version& shown, bool is_default) -> dpp::message;

/// The versions, newest first, for `history`.
[[nodiscard]] auto render_history(llm::document_kind kind, std::span<const llm::document_version> versions) -> std::string;

/// "saved as version 3 (about 250 tokens)", with a warning when a document
/// has grown expensive to send with every message
/// (src/modules/llm/docs/Language_Model.md §3.5).
[[nodiscard]] auto describe_saved(llm::document_kind kind, int version, std::string_view content) -> std::string;

/// The settings panel (src/modules/llm/docs/Language_Model.md §2.9): every value, a
/// menu that opens a group's form, and the switch.
[[nodiscard]] auto render_llm_settings(const std::map<std::string, std::int64_t, std::less<>>& values, bool enabled) -> dpp::message;

/// The form for one group of settings, filled with what they are now.
/// Nothing for a group that does not exist.
[[nodiscard]] auto llm_settings_form(std::string_view group, const std::map<std::string, std::int64_t, std::less<>>& values)
    -> std::optional<dpp::interaction_modal_response>;

/// What a group's form sent, as the values to store. Every field has to be
/// valid or nothing is stored, and the reason names the first that is not,
/// with its range (src/modules/llm/docs/Language_Model.md §2.9).
[[nodiscard]] auto read_llm_settings_form(std::string_view group, const std::map<std::string, std::string, std::less<>>& fields)
    -> std::variant<std::vector<std::pair<std::string_view, std::int64_t>>, std::string>;

/// One page of `/memory list`: whose, or everyone's when `subject` is empty.
[[nodiscard]] auto render_memories(std::span<const llm::memory> memories, std::size_t total, int page,
                                   std::optional<dpp::snowflake> subject) -> dpp::message;

/// How many memories one page shows.
inline constexpr std::size_t memories_per_page = 10;

// The panels' views.
inline constexpr std::string_view llm_settings_view = "llmset";
inline constexpr std::string_view llm_settings_pick_view = "llmsetpick";
inline constexpr std::string_view llm_settings_form_view = "llmsetform";
inline constexpr std::string_view llm_switch_view = "llmswitch";
inline constexpr std::string_view llm_document_form_view = "llmdoc";
inline constexpr std::string_view memory_list_view = "memlist";

// --------------------------------------------------------------------------
// Commands
// --------------------------------------------------------------------------

/// Everything `/llm` and `/memory` work with.
struct llm_command_services {
    config::guild_settings* settings = nullptr;
    /// config.json's llm section.
    const llm::llm_config* section = nullptr;
    llm::document_store* documents = nullptr;
    llm::advanced_trigger_store* triggers = nullptr;
    llm::blacklist_store* blacklist = nullptr;
    llm::memory_store* memories = nullptr;
    /// To show the people a memory names as mentions, not aliases.
    const llm::alias_store* aliases = nullptr;
    const llm::usage_store* usage = nullptr;

    /// For documents attached as files.
    ports::http_client* http = nullptr;
    ports::clock* clock = nullptr;

    /// Whether a key is set for this provider.
    std::function<bool(llm::provider_kind)> has_provider;
};

/// `/llm`: the switch, the model, the settings panel, the documents, the
/// advanced triggers and the blacklist
/// (src/modules/llm/docs/Language_Model.md §2.9).
///
/// Open to everyone, since the personality is
/// (src/modules/llm/docs/Language_Model.md §2.9); everything that changes how the
/// model behaves for the whole server checks Manage Server itself, as default
/// permissions are per command (src/core/docs/Commands_and_Panels.md §2.1).
class llm_command final : public command {
public:
    explicit llm_command(llm_command_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    /// The subcommands that need Manage Server, once that is checked.
    auto manage(const dpp::slashcommand_t& event, std::string_view path, std::string_view group, std::string_view action)
        -> dpp::task<void>;
    auto status(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto switch_to(const dpp::slashcommand_t& event, bool on) -> dpp::task<void>;
    auto model(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto document(const dpp::slashcommand_t& event, llm::document_kind kind, std::string_view action) -> dpp::task<void>;
    auto edit_document(const dpp::slashcommand_t& event, llm::document_kind kind) -> dpp::task<void>;
    auto diff_document(const dpp::slashcommand_t& event, llm::document_kind kind) -> dpp::task<void>;
    auto revert_document(const dpp::slashcommand_t& event, llm::document_kind kind) -> dpp::task<void>;
    auto edit_from_file(const dpp::slashcommand_t& event, llm::document_kind kind, dpp::snowflake attachment) -> dpp::task<void>;
    auto editors(const dpp::slashcommand_t& event) -> dpp::task<void>;
    auto trigger(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void>;
    auto blacklist(const dpp::slashcommand_t& event, std::string_view action) -> dpp::task<void>;

    /// Whether the invoker may change the document, answering them if not.
    [[nodiscard]] auto may_change(const dpp::slashcommand_t& event, llm::document_kind kind) const -> bool;

    command_info info_;
    llm_command_services services_;
};

/// `/memory list | forget | clear` (src/modules/llm/docs/Language_Model.md §2.4).
/// Admins see and remove everything; anyone else sees and removes what is
/// about them.
class memory_command final : public command {
public:
    explicit memory_command(llm_command_services services);

    [[nodiscard]] auto info() const -> const command_info& override { return info_; }
    [[nodiscard]] auto build(const std::string& name, dpp::snowflake application_id) const -> dpp::slashcommand override;
    auto execute(const dpp::slashcommand_t& event) -> dpp::task<void> override;

private:
    auto clear(const dpp::slashcommand_t& event, bool admin) -> dpp::task<void>;

    command_info info_;
    llm_command_services services_;
};

/// The settings panel's buttons and forms, the document form, and the
/// memory list's pages. Routes its own, as the voice lab does.
class llm_panels {
public:
    explicit llm_panels(llm_command_services services);

    /// The settings panel for `guild`, as `/llm settings` opens it.
    [[nodiscard]] auto settings_panel(dpp::snowflake guild) const -> dpp::message;

    /// False when the view is not one of these.
    auto on_component(const dpp::interaction_create_t& event, const ui::page_state& state, const std::string& chosen) -> bool;

    /// False when the form is not one of these.
    [[nodiscard]] auto on_form(const dpp::form_submit_t& event, const ui::page_state& state) const -> bool;

private:
    [[nodiscard]] auto values(dpp::snowflake guild) const -> std::map<std::string, std::int64_t, std::less<>>;

    llm_command_services services_;
};

} // namespace latibot::commands
