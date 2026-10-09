#pragma once

#include "core/util/env.hpp"

#include <cstdlib>
#include <optional>
#include <string>

namespace latibot::testing {

/// Sets an environment variable for the duration of a test.
class scoped_env {
public:
    scoped_env(const char* name, const char* value) : name_(name) {
        previous_ = util::env_var(name);
        set(value);
    }

    scoped_env(const scoped_env&) = delete;
    auto operator=(const scoped_env&) -> scoped_env& = delete;

    ~scoped_env() { set(previous_ ? previous_->c_str() : nullptr); }

private:
    /// Null or empty removes it, as `_putenv_s` does with an empty value.
    auto set(const char* value) const -> void {
#ifdef _MSC_VER
        _putenv_s(name_, value != nullptr ? value : "");
#else
        if (value == nullptr || *value == '\0') {
            unsetenv(name_);
        } else {
            setenv(name_, value, 1);
        }
#endif
    }

    const char* name_;
    std::optional<std::string> previous_;
};

} // namespace latibot::testing
