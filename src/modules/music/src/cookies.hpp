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
#include <vector>

namespace latibot::music {

// Signing yt-dlp in to an account (src/modules/music/docs/Music.md §4.9), so
// age-restricted videos play: a Firefox profile kept signed in for the bot,
// named by `LATIBOT_YTDLP_FIREFOX_PROFILE`, which yt-dlp reads itself on
// each run, or a cookies file exported from a browser, named by
// `LATIBOT_YTDLP_COOKIES`. Their values are never kept or logged: each is
// read once at startup to count what is in it, by name.
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

/// What a Firefox profile holds, as far as yt-dlp cares.
struct firefox_check {
    /// Its cookies, counted as a file's are; `json` and `malformed` unused.
    cookie_file_check found;
    /// Firefox has cookies it has not yet written into `cookies.sqlite`,
    /// which is all yt-dlp reads: it is open with this profile, or was not
    /// closed properly.
    bool unsaved = false;
    /// Why it could not be read; empty when it could.
    std::string problem;
};

/// Counts the cookies in a Firefox profile's `cookies.sqlite`, read from a
/// copy made in `scratch`, as yt-dlp reads it: Firefox may have the file
/// open, and the bot never writes to it.
[[nodiscard]] auto check_firefox_profile(const std::filesystem::path& profile, const std::filesystem::path& scratch) -> firefox_check;

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

/// What a signed-in run of yt-dlp is given: the arguments that sign it in,
/// and the copy of the cookies they name, if any, to keep until it ends.
struct sign_in_run {
    std::vector<std::string> arguments;
    std::optional<cookie_copy> copy;
};

/// The account yt-dlp signs in as: a Firefox profile, which yt-dlp reads
/// itself with `--cookies-from-browser`, or the owner's cookies file, handed
/// to each run as a copy of its own.
///
/// yt-dlp writes a cookies file back to the file it was given when it ends,
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

    /// A cookies file, copied into `copies` for each run.
    cookie_source(std::filesystem::path file, std::filesystem::path copies);

    /// A Firefox profile's folder. yt-dlp copies its `cookies.sqlite` itself
    /// and never writes to it, so nothing is copied here.
    [[nodiscard]] static auto firefox(std::filesystem::path profile) -> cookie_source;

    /// The arguments for one signed-in run, or nothing, logged, when the
    /// cookies file cannot be copied any more: that run goes signed out.
    [[nodiscard]] auto sign_in() const -> std::optional<sign_in_run>;

    /// A fresh copy of the cookies file; nothing for a Firefox profile, or,
    /// logged, when the file cannot be copied.
    [[nodiscard]] auto copy() const -> std::optional<cookie_copy>;

    /// The cookies file; empty for a Firefox profile.
    [[nodiscard]] auto file() const -> const std::filesystem::path& { return file_; }

    /// The Firefox profile; empty for a cookies file.
    [[nodiscard]] auto profile() const -> const std::filesystem::path& { return profile_; }

    /// Notes that `url` needed signing in. Safe from any thread.
    auto remember(const std::string& url) const -> void;

    /// Whether `url` needed signing in before.
    [[nodiscard]] auto needed_for(const std::string& url) const -> bool;

private:
    struct memory {
        std::mutex mutex;
        std::unordered_set<std::string> links;
    };

    cookie_source() = default;

    std::filesystem::path file_;
    std::filesystem::path copies_;
    std::filesystem::path profile_;
    std::shared_ptr<memory> memory_ = std::make_shared<memory>();
};

/// The account `LATIBOT_YTDLP_FIREFOX_PROFILE` or `LATIBOT_YTDLP_COOKIES`
/// named, and what was found in it.
struct cookie_status {
    /// In use. Nothing when none was named, or `problem` says why not.
    std::optional<cookie_source> source;
    /// The Firefox profile named, made absolute; empty when none was. It
    /// is used rather than a file when both are named.
    std::filesystem::path profile;
    /// The cookies file named, made absolute; empty when none was.
    std::filesystem::path file;
    /// Both were named, and the file is not used.
    bool both_named = false;
    cookie_file_check found;
    /// For a profile: Firefox has cookies not yet saved where yt-dlp reads.
    bool unsaved = false;
    /// Why what was named is not used.
    std::string problem;

    /// What was named, the profile first; empty when nothing was.
    [[nodiscard]] auto named() const -> const std::filesystem::path& { return profile.empty() ? file : profile; }
};

/// Checks the profile or file named, if any, and clears copies an earlier
/// run left in `copies`, which it does whatever is named: they are someone's
/// sign-in. Logs nothing; the bot says what it found at startup.
[[nodiscard]] auto load_cookies(const std::optional<std::filesystem::path>& file,
                                const std::optional<std::filesystem::path>& firefox_profile, const std::filesystem::path& copies)
    -> cookie_status;

} // namespace latibot::music
