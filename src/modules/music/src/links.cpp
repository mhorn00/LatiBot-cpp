#include "links.hpp"

#include "core/util/text.hpp"
#include "core/util/url_scan.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace latibot::music {
namespace {

constexpr std::string_view private_refusal = "that link points into a private network, which i won't fetch from";

/// Whether the name is the machine itself, which resolves locally whatever
/// DNS says.
auto names_this_machine(std::string_view host) -> bool {
    return host == "localhost" || host.ends_with(".localhost");
}

/// Parses an address written out, IPv4 or IPv6. Sets `is_address` when it
/// was one, and says whether it is private.
auto literal_is_private(const std::string& host, bool& is_address) -> bool {
    std::array<std::uint8_t, 4> v4{};
    if (inet_pton(AF_INET, host.c_str(), v4.data()) == 1) {
        is_address = true;
        return is_private(v4);
    }
    std::array<std::uint8_t, 16> v6{};
    if (inet_pton(AF_INET6, host.c_str(), v6.data()) == 1) {
        is_address = true;
        return is_private(v6);
    }
    is_address = false;
    return false;
}

/// Winsock has to be started before a name can be resolved. DPP starts it
/// in the bot, but nothing does in the tests, and starting it again only
/// counts once more.
auto start_winsock() -> void {
    static std::once_flag started;
    std::call_once(started, [] {
        WSADATA data{};
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}

} // namespace

auto host_of(std::string_view url) -> std::string {
    const auto parts = util::split_url(url);
    if (!parts) return {};

    std::string_view authority = parts->authority;
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);

    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) return {};
        return util::to_lower(authority.substr(1, close - 1));
    }
    if (const auto colon = authority.find(':'); colon != std::string_view::npos) authority = authority.substr(0, colon);
    return util::to_lower(authority);
}

auto check_link(std::string_view typed) -> checked_link {
    std::string_view text = util::trim(typed);
    if (text.size() >= 2 && text.front() == '<' && text.back() == '>') text = util::trim(text.substr(1, text.size() - 2));

    if (text.empty()) return {.url = {}, .refusal = "give me a link to play"};
    if (std::ranges::any_of(text, [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; })) {
        return {.url = {}, .refusal = "that isn't a link; i only play links for now, starting with https://"};
    }

    const auto parts = util::split_url(text);
    if (!parts) return {.url = {}, .refusal = "that isn't a link; i only play links for now, starting with https://"};

    const std::string host = host_of(text);
    if (host.empty()) return {.url = {}, .refusal = "that link has no site in it"};

    bool is_address = false;
    if (names_this_machine(host) || literal_is_private(host, is_address)) return {.url = {}, .refusal = std::string(private_refusal)};

    return {.url = std::string(text), .refusal = {}};
}

auto is_private(const std::array<std::uint8_t, 4>& ip) noexcept -> bool {
    const unsigned a = ip[0];
    const unsigned b = ip[1];
    if (a == 0 || a == 10 || a == 127) return true;    // this network, private, loopback
    if (a == 100 && (b & 0xC0U) == 64) return true;    // carrier NAT, 100.64/10
    if (a == 169 && b == 254) return true;             // link-local
    if (a == 172 && (b & 0xF0U) == 16) return true;    // private, 172.16/12
    if (a == 192 && b == 168) return true;             // private
    if (a == 192 && b == 0 && ip[2] == 0) return true; // protocol assignments, 192.0.0/24
    if (a == 198 && (b & 0xFEU) == 18) return true;    // benchmarking, 198.18/15
    return a >= 224;                                   // multicast, reserved, broadcast
}

auto is_private(const std::array<std::uint8_t, 16>& ip) noexcept -> bool {
    const auto zero_until = [&ip](std::size_t end) {
        return std::all_of(ip.begin(), ip.begin() + static_cast<std::ptrdiff_t>(end), [](auto b) { return b == 0; });
    };
    const auto embedded_v4 = [&ip] { return std::array<std::uint8_t, 4>{ip[12], ip[13], ip[14], ip[15]}; };

    if (zero_until(15) && (ip[15] == 0 || ip[15] == 1)) return true;                                        // unspecified, loopback
    if (zero_until(10) && ip[10] == 0xFF && ip[11] == 0xFF) return is_private(embedded_v4());               // IPv4-mapped
    if (ip[0] == 0x00 && ip[1] == 0x64 && ip[2] == 0xFF && ip[3] == 0x9B) return is_private(embedded_v4()); // NAT64
    if ((ip[0] & 0xFEU) == 0xFC) return true;                                                               // unique local, fc00::/7
    if (ip[0] == 0xFE && (ip[1] & 0xC0U) == 0x80) return true;                                              // link-local, fe80::/10
    return ip[0] == 0xFF;                                                                                   // multicast
}

auto resolves_to_private(const std::string& host) -> bool {
    if (names_this_machine(host)) return true;
    bool is_address = false;
    const bool literal_private = literal_is_private(host, is_address);
    if (is_address) return literal_private;

    start_winsock();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0) return false;

    bool refused = false;
    for (const addrinfo* entry = found; entry != nullptr && !refused; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET) {
            sockaddr_in address{};
            std::memcpy(&address, entry->ai_addr, sizeof(address));
            std::array<std::uint8_t, 4> v4{};
            std::memcpy(v4.data(), &address.sin_addr, v4.size());
            refused = is_private(v4);
        } else if (entry->ai_family == AF_INET6) {
            sockaddr_in6 address{};
            std::memcpy(&address, entry->ai_addr, sizeof(address));
            std::array<std::uint8_t, 16> v6{};
            std::memcpy(v6.data(), &address.sin6_addr, v6.size());
            refused = is_private(v6);
        }
    }
    freeaddrinfo(found);
    return refused;
}

} // namespace latibot::music
