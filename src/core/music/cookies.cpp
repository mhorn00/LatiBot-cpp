#include "core/music/cookies.hpp"

#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace latibot::music {
namespace {

/// What starts every copy's name, so clearing leftovers touches nothing
/// else in the folder.
constexpr std::string_view copy_prefix = "cookies-";

auto is_youtube(std::string_view domain) -> bool {
    const std::string lower = util::to_lower(domain);
    return lower == "youtube.com" || lower.ends_with(".youtube.com");
}

auto read_file(const std::filesystem::path& path) -> std::optional<std::string> {
    const std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) return std::nullopt;
    return std::move(contents).str();
}

auto remove_leftovers(const std::filesystem::path& copies) -> void {
    // Error codes throughout: a folder that is not there yet is the usual
    // case, and none of this is worth stopping startup over.
    std::error_code error;
    for (std::filesystem::directory_iterator entry(copies, error); !error && entry != std::filesystem::directory_iterator{};
         entry.increment(error)) {
        if (!entry->path().filename().string().starts_with(copy_prefix)) continue;
        std::error_code ignored;
        std::filesystem::remove(entry->path(), ignored);
    }
}

} // namespace

auto check_cookie_file(std::string_view text) -> cookie_file_check {
    constexpr std::string_view http_only = "#HttpOnly_";
    constexpr std::size_t fields = 7;

    cookie_file_check found;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    const std::string_view start = util::trim(text);
    if (start.starts_with('[') || start.starts_with('{')) {
        found.json = true;
        return found;
    }

    for (std::string_view line : util::lines(text)) {
        if (line.starts_with(http_only)) {
            line.remove_prefix(http_only.size());
        } else if (line.starts_with('#') || util::trim(line).empty()) {
            continue;
        }
        if (util::count_occurrences(line, "\t") + 1 != fields) {
            ++found.malformed;
            continue;
        }
        ++found.cookies;
        if (!is_youtube(line.substr(0, line.find('\t')))) continue;
        ++found.youtube;
        // The name is the sixth field.
        std::string_view name = line;
        for (int skip = 0; skip < 5; ++skip) {
            name.remove_prefix(name.find('\t') + 1);
        }
        name = name.substr(0, name.find('\t'));
        if (name == "SAPISID" || name == "__Secure-3PAPISID") found.youtube_sign_in = true;
    }
    return found;
}

auto needs_sign_in(std::string_view errors) -> bool {
    const std::string lower = util::to_lower(errors);
    constexpr std::array<std::string_view, 6> markers{"sign in", "--cookies", "login", "log in", "logged in", "authentication"};
    return std::ranges::any_of(markers, [&lower](std::string_view marker) { return lower.find(marker) != std::string::npos; });
}

cookie_copy::~cookie_copy() {
    remove();
}

cookie_copy::cookie_copy(cookie_copy&& other) noexcept : path_(std::exchange(other.path_, {})) {}

auto cookie_copy::operator=(cookie_copy&& other) noexcept -> cookie_copy& {
    if (this != &other) {
        remove();
        path_ = std::exchange(other.path_, {});
    }
    return *this;
}

auto cookie_copy::remove() noexcept -> void {
    if (path_.empty()) return;
    // Out of a destructor, so nothing may escape. One that cannot be
    // removed, still held open by a yt-dlp that is ending, goes at the next
    // start.
    try {
        std::error_code error;
        if (!std::filesystem::remove(path_, error) && error) {
            util::log().debug("could not remove a copy of the yt-dlp cookies; the next start will: {}", error.message());
        }
    } catch (...) { // NOLINT(bugprone-empty-catch)
    }
    path_.clear();
}

cookie_source::cookie_source(std::filesystem::path file, std::filesystem::path copies)
    : file_(std::move(file)), copies_(std::move(copies)) {}

auto cookie_source::copy() const -> std::optional<cookie_copy> {
    // Random, not counted: a name that cannot be guessed, and no clash with
    // a copy left behind by an earlier run.
    thread_local std::mt19937_64 random{std::random_device{}()};
    const std::filesystem::path target = copies_ / std::format("{}{:016x}.txt", copy_prefix, random());

    std::error_code error;
    std::filesystem::create_directories(copies_, error);
    if (!error) std::filesystem::copy_file(file_, target, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        util::log().warn("could not copy the yt-dlp cookies in {}, so this goes signed out: {}", file_.generic_string(), error.message());
        return std::nullopt;
    }
    return cookie_copy(target);
}

auto cookie_source::remember(const std::string& url) const -> void {
    const std::scoped_lock lock(memory_->mutex);
    if (memory_->links.size() >= remembered_links) memory_->links.clear();
    memory_->links.insert(url);
}

auto cookie_source::needed_for(const std::string& url) const -> bool {
    const std::scoped_lock lock(memory_->mutex);
    return memory_->links.contains(url);
}

auto load_cookies(const std::optional<std::filesystem::path>& file, const std::filesystem::path& copies) -> cookie_status {
    remove_leftovers(copies);

    cookie_status status;
    if (!file || file->empty()) return status;

    std::error_code error;
    status.file = std::filesystem::absolute(*file, error);
    if (error) status.file = *file;

    const std::optional<std::string> text = read_file(status.file);
    if (!text) {
        status.problem = "could not be read";
        return status;
    }
    status.found = check_cookie_file(*text);
    if (status.found.json) {
        status.problem = "is JSON, and yt-dlp only reads cookies in the Netscape cookies.txt format";
    } else if (status.found.cookies == 0) {
        status.problem = "has no cookies in it";
    } else {
        status.source.emplace(status.file, copies);
    }
    return status;
}

} // namespace latibot::music
