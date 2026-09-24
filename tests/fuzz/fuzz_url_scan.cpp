// Fuzzes the link scanner and replacement planning, which see every message
// (plan v4 §9.1, §17.5).

#include "core/events/url_rules.hpp"
#include "core/util/text.hpp"
#include "core/util/url_scan.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    std::size_t previous_end = 0;
    for (const latibot::util::found_link& link : latibot::util::find_links(text)) {
        // In order, inside the text, not overlapping, and exactly the text
        // the offsets describe.
        if (link.begin < previous_end || link.end > text.size() || link.begin >= link.end) {
            std::abort();
        }
        if (text.substr(link.begin, link.end - link.begin) != link.url) {
            std::abort();
        }
        if (!latibot::util::split_url(link.url)) {
            std::abort();
        }
        // A link is spoilered exactly when an odd number of markers precede it.
        if (link.spoilered != latibot::util::is_inside_spoiler(text.substr(0, link.begin))) {
            std::abort();
        }
        previous_end = link.end;
    }

    static const std::vector<latibot::events::url_rule> rules{
        {.domain = "x.com", .mirrors = {{.host = "fxtwitter.com", .translate_suffix = "/en"}}},
    };
    for (const latibot::events::planned_link& link : latibot::events::plan_replacements(text, rules)) {
        (void)latibot::events::mirror_url(link, 0);
    }
    return 0;
}
