#pragma once

#include <map>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>

namespace latibot::module {

/// What modules offer each other: one implementation per interface
/// (docs/modules/Module_Plan_Final.md §5.2).
///
/// The interfaces live in `core/capabilities/`. A module offers one in
/// `module::offer`, and another looks it up in `module::start`, when every
/// offer has been made. Looking up one nobody offers gives null, which is how
/// a module copes with the other not being built: the language model never
/// speaks without dectalk, and that is all.
///
/// Called only while the bot starts, on one thread; afterwards it is only
/// read.
class capability_registry {
public:
    /// Offers `implementation` as the `Interface`. It must outlive the
    /// registry, which a module's member does. Throws std::logic_error when
    /// another module already offers it: which one would be found is not
    /// something to leave to the start order.
    template <typename Interface>
    auto offer(Interface& implementation, std::string_view by) -> void {
        add(typeid(Interface), &implementation, by);
    }

    /// The `Interface`'s implementation, or null when nobody offers it.
    template <typename Interface>
    [[nodiscard]] auto find() const -> Interface* {
        return static_cast<Interface*>(lookup(typeid(Interface)));
    }

    /// Who offers the `Interface`, for the startup log; empty when nobody does.
    template <typename Interface>
    [[nodiscard]] auto offered_by() const -> std::string_view {
        return owner(typeid(Interface));
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return offered_.size(); }

private:
    struct offering {
        void* implementation = nullptr;
        std::string by;
    };

    auto add(const std::type_info& wanted, void* implementation, std::string_view by) -> void;
    [[nodiscard]] auto lookup(const std::type_info& wanted) const -> void*;
    [[nodiscard]] auto owner(const std::type_info& wanted) const -> std::string_view;

    std::map<std::type_index, offering> offered_;
};

} // namespace latibot::module
