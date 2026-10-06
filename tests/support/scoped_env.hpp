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
    auto set(const char* value) const -> void { _putenv_s(name_, value != nullptr ? value : ""); }

    const char* name_;
    std::optional<std::string> previous_;
};

} // namespace latibot::testing
