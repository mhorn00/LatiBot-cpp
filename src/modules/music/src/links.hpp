#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace latibot::music {

// What `/music play` accepts (src/modules/music/docs/Music.md §6): an http(s) link,
// and never one into the host's own network, since yt-dlp would fetch it
// from there.

/// A link as typed, checked.
struct checked_link {
    /// The link to hand yt-dlp, when it passed.
    std::string url;

    /// Why it did not, fit to show whoever typed it; empty when it passed.
    std::string refusal;

    [[nodiscard]] auto ok() const noexcept -> bool { return refusal.empty(); }
};

/// Checks what was typed for `/music play`. Surrounding spaces and the
/// `<…>` Discord uses to hide a preview are dropped. The host must be
/// named, and must not be written as a private or loopback address or as
/// `localhost`; a name that resolves to one is caught by
/// `resolves_to_private`, which needs the network.
[[nodiscard]] auto check_link(std::string_view typed) -> checked_link;

/// The host part of an http(s) link: no credentials, port, or the brackets
/// round an IPv6 address. Empty when there is none.
[[nodiscard]] auto host_of(std::string_view url) -> std::string;

/// Whether an address is one the host should never be asked to fetch from:
/// private, loopback, link-local, carrier NAT, multicast, reserved, or
/// unspecified.
[[nodiscard]] auto is_private(const std::array<std::uint8_t, 4>& ipv4) noexcept -> bool;
[[nodiscard]] auto is_private(const std::array<std::uint8_t, 16>& ipv6) noexcept -> bool;

/// Whether `host` is, or resolves to, any such address. A name that does not
/// resolve is not refused here; yt-dlp says what is wrong with it.
[[nodiscard]] auto resolves_to_private(const std::string& host) -> bool;

} // namespace latibot::music
