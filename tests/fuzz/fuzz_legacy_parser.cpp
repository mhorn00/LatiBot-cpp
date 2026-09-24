// Fuzzes recognising the bot's old replacements, which reads years of
// whatever people wrote (plan v4 §9.7, §17.5).

#include "core/events/legacy_replacements.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    static const latibot::events::mirror_map mirrors{{"fxtwitter.com", "x.com"}, {"vxtwitter.com", "x.com"}};
    constexpr dpp::snowflake bot{42};

    // The first half is the replacement, the second an earlier message.
    const std::size_t split = text.size() / 2;
    latibot::events::history_message ours{.id = dpp::snowflake{900},
                                          .author_id = bot,
                                          .author_is_bot = true,
                                          .webhook_id = {},
                                          .is_system = false,
                                          .replied_to = {},
                                          .content = std::string(text.substr(0, split)),
                                          .reactions = {}};
    const std::vector<latibot::events::history_message> older{{.id = dpp::snowflake{800},
                                                               .author_id = dpp::snowflake{11},
                                                               .author_is_bot = false,
                                                               .webhook_id = {},
                                                               .is_system = false,
                                                               .replied_to = {},
                                                               .content = std::string(text.substr(split)),
                                                               .reactions = {}}};

    const auto match = latibot::events::classify(ours, bot, mirrors);
    for (const std::string& url : match.mirror_urls) {
        // Every mirror link is a piece of the message itself.
        if (ours.content.find(url) == std::string::npos) {
            std::abort();
        }
    }

    if (match.what == latibot::events::legacy_match::kind::recognised) {
        const auto found = latibot::events::attribute(ours, match, older, bot);
        // Only the one earlier message can be credited.
        if (found.author_id && *found.author_id != dpp::snowflake{11}) {
            std::abort();
        }
    }
    return 0;
}
