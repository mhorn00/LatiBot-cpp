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
                ("latibot-test-" + std::to_string(counter.fetch_add(1)) + "-" +
                 std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        std::filesystem::create_directories(path_);
    }

    temp_directory(const temp_directory&) = delete;
    temp_directory& operator=(const temp_directory&) = delete;

    ~temp_directory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] std::filesystem::path file(std::string_view name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

} // namespace latibot::testing
