#pragma once

#include "core/db/schema_versions.hpp"
#include "core/modules/module.hpp"

#include <memory>

namespace latibot::modules {
class host;
}

namespace latibot::links {

/// The links module (README.md, src/modules/links/docs/Url_Replacement.md):
/// `/links`, `/urltoggle`, and replacing links with mirrors that preview.
[[nodiscard]] auto make_module(modules::host& bot) -> std::unique_ptr<modules::module>;

/// Its tables, version 1 first. Public, because linkstats' rows refer to
/// `replacement_messages`, so its tests need these tables too.
[[nodiscard]] auto schema() noexcept -> db::module_schema;

} // namespace latibot::links
