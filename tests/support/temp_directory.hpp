#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace latibot::testing {

/// A unique directory under the system temp path, removed on destruction.
///
/// Database tests mostly use ":memory:", but the backup tests need real files.
class temp_directory {
public:
    temp_directory() {
        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path() /
                ("latibot-test-" + std::to_string(counter.fetch_add(1)) + "-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(path_);
    }

    temp_directory(const temp_directory&) = delete;
    auto operator=(const temp_directory&) -> temp_directory& = delete;

    ~temp_directory() {
        // The error_code overload still allocates while it walks the tree, so
        // it can throw bad_alloc even though it reports everything else
        // through the code. Out of a destructor that would take the whole
        // test run down instead of failing one test.
        try {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        } catch (...) { // NOLINT(bugprone-empty-catch)
            // Leaving a directory behind in %TEMP% is not worth reporting,
            // and there is nowhere to report it from here.
        }
    }

    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& { return path_; }

    [[nodiscard]] auto file(std::string_view name) const -> std::filesystem::path { return path_ / name; }

private:
    std::filesystem::path path_;
};

} // namespace latibot::testing
