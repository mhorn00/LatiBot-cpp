// Fuzzes the text helpers that run over every message, and the ones that cut
// text down to Discord's limits (plan §17.5).
//
// Each check is a property of the result that the helper's own code does not
// state, so a wrong implementation can fail it.

#include "core/util/text.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view whitespace = " \t\n\r\f\v";

bool is_space(char letter) {
    return whitespace.find(letter) != std::string_view::npos;
}

bool is_continuation(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

void require(bool holds) {
    if (!holds) {
        std::abort();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    using namespace latibot::util;

    // trim returns a piece of the text itself, with whitespace only on either
    // side of it and none at its own ends.
    const std::string_view trimmed = trim(text);
    if (trimmed.empty()) {
        for (const char letter : text) {
            require(is_space(letter));
        }
    } else {
        require(trimmed.data() >= text.data() && trimmed.data() + trimmed.size() <= text.data() + text.size());
        require(!is_space(trimmed.front()) && !is_space(trimmed.back()));
        const auto before = static_cast<std::size_t>(trimmed.data() - text.data());
        for (std::size_t at = 0; at < before; ++at) {
            require(is_space(text[at]));
        }
        for (std::size_t at = before + trimmed.size(); at < text.size(); ++at) {
            require(is_space(text[at]));
        }
    }
    require(is_blank(text) == trimmed.empty());

    // Occurrences do not overlap, so there cannot be more than fit.
    require(count_occurrences(text, "||") <= text.size() / 2);
    require(count_occurrences(text, "") == 0);

    // One character per byte that starts one: never more than the bytes,
    // and exactly the bytes for plain ASCII.
    const std::size_t characters = character_count(text);
    require(characters <= text.size());
    bool ascii = true;
    for (const char letter : text) {
        ascii = ascii && static_cast<unsigned char>(letter) < 0x80U;
    }
    require(!ascii || characters == text.size());

    // Cutting to any limit keeps within it, changes nothing that fits, and
    // otherwise keeps a prefix that ends where a character starts.
    const std::size_t limit = size == 0 ? 0 : data[0] % 64;
    const std::string cut = truncate(text, limit);
    require(character_count(cut) <= limit);
    if (characters <= limit) {
        require(cut == text);
    } else if (limit > 0) {
        constexpr std::string_view ellipsis = "…";
        require(cut.ends_with(ellipsis));
        const std::string_view kept = std::string_view(cut).substr(0, cut.size() - ellipsis.size());
        require(text.starts_with(kept));
        require(kept.size() < text.size() && !is_continuation(text[kept.size()]));
        require(character_count(kept) == limit - 1);
    } else {
        require(cut.empty());
    }
    return 0;
}
