#include "cookies.hpp"

#include "core/db/database.hpp"
#include "core/db/statement.hpp"
#include "core/util/log.hpp"
#include "core/util/text.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
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

/// One cookie, by where it is for and its name, never its value.
auto count_cookie(cookie_file_check& found, std::string_view domain, std::string_view name) -> void {
    ++found.cookies;
    if (!is_youtube(domain)) return;
    ++found.youtube;
    if (name == "SAPISID" || name == "__Secure-3PAPISID") found.youtube_sign_in = true;
}

auto absolute_or_as_is(const std::filesystem::path& path) -> std::filesystem::path {
    std::error_code error;
    const std::filesystem::path made = std::filesystem::absolute(path, error);
    return error ? path : made;
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
        // The name is the sixth field.
        std::string_view name = line;
        for (int skip = 0; skip < 5; ++skip) {
            name.remove_prefix(name.find('\t') + 1);
        }
        count_cookie(found, line.substr(0, line.find('\t')), name.substr(0, name.find('\t')));
    }
    return found;
}

auto check_firefox_profile(const std::filesystem::path& profile, const std::filesystem::path& scratch) -> firefox_check {
    firefox_check result;
    std::error_code error;
    if (!std::filesystem::is_directory(profile, error)) {
        result.problem = "is not a folder";
        return result;
    }
    const std::filesystem::path database = profile / "cookies.sqlite";
    if (!std::filesystem::is_regular_file(database, error)) {
        result.problem = "has no cookies.sqlite yet: open Firefox with it, sign in to YouTube, and close Firefox";
        return result;
    }
    // Firefox writes its newest cookies to the write-ahead log first, and
    // into cookies.sqlite when it closes.
    const std::uintmax_t pending = std::filesystem::file_size(profile / "cookies.sqlite-wal", error);
    result.unsaved = !error && pending > 0;

    std::filesystem::create_directories(scratch, error);
    const std::filesystem::path copy = scratch / std::format("{}firefox-check.sqlite", copy_prefix);
    std::filesystem::copy_file(database, copy, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        result.problem = "has a cookies.sqlite that could not be copied: " + error.message();
        return result;
    }
    try {
        db::database cookies(copy);
        db::statement rows = cookies.prepare("SELECT host, name FROM moz_cookies");
        while (rows.step()) {
            count_cookie(result.found, rows.get<std::string>(0), rows.get<std::string>(1));
        }
    } catch (const std::exception& failure) {
        result.problem = std::string("has a cookies.sqlite that could not be read: ") + failure.what();
    }
    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::filesystem::remove(copy.string() + suffix, error);
    }
    return result;
}

auto needs_sign_in(std::string_view errors) -> bool {
    const std::string lower = util::to_lower(errors);
    constexpr std::array<std::string_view, 6> markers{"sign in", "--cookies", "login", "log in", "logged in", "authentication"};
    return std::ranges::any_of(markers, [&lower](std::string_view marker) { return lower.contains(marker); });
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

auto cookie_source::firefox(std::filesystem::path profile) -> cookie_source {
    cookie_source source;
    source.profile_ = std::move(profile);
    return source;
}

auto cookie_source::sign_in() const -> std::optional<sign_in_run> {
    if (!profile_.empty()) {
        return sign_in_run{.arguments = {"--cookies-from-browser", "firefox:" + profile_.string()}, .copy = std::nullopt};
    }
    std::optional<cookie_copy> made = copy();
    if (!made) return std::nullopt;
    std::vector<std::string> arguments{"--cookies", made->path().string()};
    return sign_in_run{.arguments = std::move(arguments), .copy = std::move(made)};
}

auto cookie_source::copy() const -> std::optional<cookie_copy> {
    if (file_.empty()) return std::nullopt;
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

auto load_cookies(const std::optional<std::filesystem::path>& file, const std::optional<std::filesystem::path>& firefox_profile,
                  const std::filesystem::path& copies) -> cookie_status {
    remove_leftovers(copies);

    cookie_status status;
    const bool has_file = file && !file->empty();
    if (has_file) status.file = absolute_or_as_is(*file);

    if (firefox_profile && !firefox_profile->empty()) {
        status.profile = absolute_or_as_is(*firefox_profile);
        status.both_named = has_file;
        const firefox_check check = check_firefox_profile(status.profile, copies);
        status.found = check.found;
        status.unsaved = check.unsaved;
        status.problem = check.problem;
        if (status.problem.empty() && status.found.cookies == 0) {
            status.problem = "has no cookies in it yet: open Firefox with it, sign in to YouTube, and close Firefox";
        }
        if (status.problem.empty()) status.source = cookie_source::firefox(status.profile);
        return status;
    }
    if (!has_file) return status;

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
