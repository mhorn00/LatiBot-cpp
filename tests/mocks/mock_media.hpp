#pragma once

#include "core/ports/media.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace latibot::testing {

/// How a scripted track behaves when it is played.
struct scripted_track {
    /// Samples it produces, all of the value `level`.
    std::size_t samples = 0;
    std::int16_t level = 1000;

    /// After producing its samples it fails with this, rather than
    /// finishing; empty to finish.
    std::string fails_with;

    /// Produces nothing, and stays running, until `release` is set: a track
    /// still being fetched.
    bool held = false;
};

/// A stream of a scripted track.
class scripted_stream final : public ports::pcm_stream {
public:
    explicit scripted_stream(scripted_track script, std::shared_ptr<bool> release)
        : script_(std::move(script)), release_(std::move(release)) {}

    auto read(std::span<std::int16_t> into) -> std::size_t override {
        if (script_.held && !*release_) return 0;
        const std::size_t count = std::min(into.size(), script_.samples - given_);
        std::fill_n(into.begin(), count, script_.level);
        given_ += count;
        return count;
    }

    [[nodiscard]] auto state() const -> ports::stream_state override {
        if (script_.held && !*release_) return ports::stream_state::running;
        if (given_ < script_.samples) return ports::stream_state::running;
        return script_.fails_with.empty() ? ports::stream_state::finished : ports::stream_state::failed;
    }

    [[nodiscard]] auto error() const -> std::string override { return state() == ports::stream_state::failed ? script_.fails_with : ""; }

private:
    scripted_track script_;
    std::shared_ptr<bool> release_;
    std::size_t given_ = 0;
};

/// Opens scripted tracks by URL, and records every open.
class mock_opener final : public ports::stream_opener {
public:
    std::map<std::string, scripted_track> tracks;
    std::vector<std::string> opened;

    /// Releases every held track at once.
    std::shared_ptr<bool> release = std::make_shared<bool>(false);

    auto open(const std::string& url) -> std::unique_ptr<ports::pcm_stream> override {
        opened.push_back(url);
        const auto found = tracks.find(url);
        scripted_track script =
            found == tracks.end() ? scripted_track{.samples = 0, .level = 0, .fails_with = "no such track", .held = false} : found->second;
        return std::make_unique<scripted_stream>(std::move(script), release);
    }

    [[nodiscard]] auto times_opened(const std::string& url) const -> std::size_t {
        return static_cast<std::size_t>(std::ranges::count(opened, url));
    }
};

/// Answers lookups from a table, by URL.
class mock_resolver final : public ports::media_resolver {
public:
    std::map<std::string, ports::result<ports::media_lookup>> answers;
    std::vector<std::string> asked;

    auto lookup(std::string url, std::size_t max_items) -> dpp::task<ports::result<ports::media_lookup>> override {
        asked.push_back(url);
        const auto found = answers.find(url);
        if (found == answers.end()) co_return ports::api_error{.http_status = 0, .message = "there's nothing i can play at that link"};
        ports::result<ports::media_lookup> answer = found->second;
        if (answer.ok() && answer.value().items.size() > max_items) answer.value().items.resize(max_items);
        co_return answer;
    }
};

} // namespace latibot::testing
