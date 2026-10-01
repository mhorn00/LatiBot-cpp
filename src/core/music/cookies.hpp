#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace latibot::music {

// Signing yt-dlp in to an account (docs/features/Music.md §4.9): a cookies
// file the owner exported from a browser, named by `LATIBOT_YTDLP_COOKIES`,
// so age-restricted videos play. Its values are never kept or logged: the
// file is read once to count what is in it, then only ever copied.
//
// yt-dlp goes signed out first, and signs in only when it fails saying it
// must (`needs_sign_in`), so the account is used for nothing else.

/// What a cookies file holds, as far as yt-dlp cares.
struct cookie_file_check {
    /// Lines that are cookies: seven fields, separated by tabs.
    std::size_t cookies = 0;
    /// Of those, the ones for youtube.com.
    std::size_t youtube = 0;
    /// Whether one of those is `SAPISID` or `__Secure-3PAPISID`, which yt-dlp
    /// needs to sign in at all: without one, it goes signed out whatever
    /// else the file holds. Missing when the file was exported signed out.
    bool youtube_sign_in = false;
    /// Lines that are neither cookies, comments nor blank; yt-dlp skips them.
    std::size_t malformed = 0;
    /// Exported as JSON, which yt-dlp refuses: it reads Netscape's
    /// `cookies.txt` format only.
    bool json = false;
};

/// Reads a cookies file's text in Netscape's format. `#HttpOnly_` before a
/// line marks a cookie, not a comment.
[[nodiscard]] auto check_cookie_file(std::string_view text) -> cookie_file_check;

/// Whether what yt-dlp wrote to stderr says that signing in would help:
/// an age check, "not a bot", a private or members-only video. yt-dlp's
/// own errors for those say to sign in, or to pass `--cookies`.
[[nodiscard]] auto needs_sign_in(std::string_view errors) -> bool;

/// One run's copy of the cookies, removed when this goes.
class cookie_copy {
public:
    explicit cookie_copy(std::filesystem::path path) : path_(std::move(path)) {}
    ~cookie_copy();

    cookie_copy(cookie_copy&& other) noexcept;
    auto operator=(cookie_copy&& other) noexcept -> cookie_copy&;
    cookie_copy(const cookie_copy&) = delete;
    auto operator=(const cookie_copy&) -> cookie_copy& = delete;

    [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

private:
    auto remove() noexcept -> void;

    /// Empty once moved from.
    std::filesystem::path path_;
};

/// The owner's cookies file, handed to each run of yt-dlp as a copy of its
/// own.
///
/// yt-dlp writes its cookies back to the file it was given when it ends,
/// by truncating and rewriting it. Up to four run at once, two reading
/// links and two fetching tracks, and one rewriting the file while another
/// reads it would leave that one signed out, or the file spoilt. A copy
/// each, in `copies`, leaves the owner's file as it was exported.
///
/// It also remembers the links that needed signing in, so a track read
/// signed in is fetched signed in, rather than refused once more first.
/// Copies of a `cookie_source` share what they remember.
class cookie_source {
public:
    /// How many links are remembered; past it, they are forgotten and
    /// remembered afresh.
    static constexpr std::size_t remembered_links = 1000;

    cookie_source(std::filesystem::path file, std::filesystem::path copies);

    /// A fresh copy, or nothing, logged, when the file cannot be copied any
    /// more: that run goes signed out.
    [[nodiscard]] auto copy() const -> std::optional<cookie_copy>;

    [[nodiscard]] auto file() const -> const std::filesystem::path& { return file_; }

    /// Notes that `url` needed signing in. Safe from any thread.
    auto remember(const std::string& url) const -> void;

    /// Whether `url` needed signing in before.
    [[nodiscard]] auto needed_for(const std::string& url) const -> bool;

private:
    struct memory {
        std::mutex mutex;
        std::unordered_set<std::string> links;
    };

    std::filesystem::path file_;
    std::filesystem::path copies_;
    std::shared_ptr<memory> memory_ = std::make_shared<memory>();
};

/// The cookies `LATIBOT_YTDLP_COOKIES` named, and what was found in them.
struct cookie_status {
    /// In use. Nothing when none was named, or `problem` says why not.
    std::optional<cookie_source> source;
    /// The file named, made absolute; empty when none was.
    std::filesystem::path file;
    cookie_file_check found;
    /// Why the file named is not used.
    std::string problem;
};

/// Checks the file named, if any, and clears copies an earlier run left in
/// `copies`, which it does whether or not a file is named: they are
/// someone's sign-in. Logs nothing; the bot says what it found at startup.
[[nodiscard]] auto load_cookies(const std::optional<std::filesystem::path>& file, const std::filesystem::path& copies) -> cookie_status;

} // namespace latibot::music
