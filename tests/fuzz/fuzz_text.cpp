// Fuzzes the text helpers that run over every message (plan v4 §17.5).

#include "core/util/text.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);

    const std::size_t markers = latibot::util::count_occurrences(text, "||");
    const bool spoilered = latibot::util::is_inside_spoiler(text);

    // The two must agree: a spoiler is open exactly when the count is odd.
    if (spoilered != (markers % 2 == 1)) {
        std::abort();
    }
    return 0;
}
