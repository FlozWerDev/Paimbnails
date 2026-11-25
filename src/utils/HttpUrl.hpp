#pragma once

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace paimon::net {

struct HttpUrl {
    std::string scheme;
    std::string host;
    uint16_t port = 0;

    bool sameOrigin(HttpUrl const& other) const {
        return scheme == other.scheme && host == other.host && port == other.port;
    }
};

inline std::optional<HttpUrl> parseHttpUrl(std::string_view url) {
    if (std::any_of(url.begin(), url.end(), [](unsigned char c) {
            return c <= 0x20 || c == 0x7f || c == '\\';
        })) return std::nullopt;

    auto const schemeEnd = url.find("://");
    if (schemeEnd == std::string_view::npos) return std::nullopt;
    HttpUrl parsed;
    parsed.scheme = url.substr(0, schemeEnd);
    auto lowerAscii = [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    };
    std::transform(parsed.scheme.begin(), parsed.scheme.end(), parsed.scheme.begin(), lowerAscii);
    if (parsed.scheme != "http" && parsed.scheme != "https") return std::nullopt;
    parsed.port = parsed.scheme == "https" ? 443 : 80;

    auto authority = url.substr(schemeEnd + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    if (authority.empty() || authority.find('@') != std::string_view::npos
        || authority.find('%') != std::string_view::npos) return std::nullopt;

    std::string_view host;
    std::string_view port;
    bool explicitPort = false;
    if (authority.front() == '[') {
        auto end = authority.find(']');
        if (end == std::string_view::npos) return std::nullopt;
        host = authority.substr(1, end - 1);
        if (host.find(':') == std::string_view::npos) return std::nullopt;
        if (end + 1 < authority.size()) {
            if (authority[end + 1] != ':') return std::nullopt;
            port = authority.substr(end + 2);
            explicitPort = true;
        }
        if (std::any_of(host.begin(), host.end(), [](unsigned char c) {
                return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                    || (c >= 'A' && c <= 'F') || c == ':' || c == '.');
            })) return std::nullopt;
    } else {
        auto colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string_view::npos) {
            port = authority.substr(colon + 1);
            explicitPort = true;
        }
        if (std::any_of(host.begin(), host.end(), [](unsigned char c) {
                return !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z') || c == '-' || c == '.' || c == '_');
            })) return std::nullopt;
        if (host.ends_with('.')) host.remove_suffix(1);
    }
    if (host.empty()) return std::nullopt;
    parsed.host = host;
    std::transform(parsed.host.begin(), parsed.host.end(), parsed.host.begin(), lowerAscii);
    if (explicitPort) {
        unsigned value = 0;
        auto result = std::from_chars(port.data(), port.data() + port.size(), value);
        if (result.ec != std::errc{} || result.ptr != port.data() + port.size()
            || value == 0 || value > 65535) return std::nullopt;
        parsed.port = static_cast<uint16_t>(value);
    }
    return parsed;
}

inline std::optional<std::string> resolveHttpRedirect(std::string_view base, std::string_view location) {
    auto const baseUrl = parseHttpUrl(base);
    if (!baseUrl || location.empty()) return std::nullopt;
    std::string target;
    auto const schemeEnd = base.find("://");
    auto const authorityEnd = base.find_first_of("/?#", schemeEnd + 3);
    auto const origin = base.substr(0, authorityEnd);
    if (location.starts_with("//")) {
        target = std::string(base.substr(0, schemeEnd + 1)) + std::string(location);
    } else if (location.starts_with('/')) {
        target = std::string(origin) + std::string(location);
    } else if (location.find("://") != std::string_view::npos) {
        target = location;
    } else if (location.starts_with('?')) {
        target = std::string(base.substr(0, base.find_first_of("?#"))) + std::string(location);
    } else if (location.starts_with('#')) {
        target = std::string(base.substr(0, base.find('#'))) + std::string(location);
    } else {
        auto path = base.substr(0, base.find_first_of("?#"));
        auto const slash = path.find_last_of('/');
        target = std::string(origin);
        if (slash != std::string_view::npos && slash >= origin.size()) {
            target += path.substr(origin.size(), slash - origin.size() + 1);
        } else {
            target += '/';
        }
        target += location;
    }
    auto parsed = parseHttpUrl(target);
    if (!parsed || (baseUrl->scheme == "https" && parsed->scheme != "https")) {
        return std::nullopt;
    }
    return target;
}

// rejects loopback, private, link-local, CGNAT, multicast and ambiguous
// numeric hosts. Used before server-provided URLs reach the HTTP client.
inline bool isPublicHttpUrl(std::string_view url) {
    auto parsed = parseHttpUrl(url);
    if (!parsed) return false;
    auto const& host = parsed->host;
    if (host.empty()) return false;

    // iPv6 literals: require valid hex/colons only and global unicast (2000::/3),
    // rejecting loopback, link-local, unique-local, IPv4-mapped (::ffff:) and documentation (2001:db8::).
    if (host.find(':') != std::string::npos) {
        if (host.size() < 4 || host.front() == ':' || (host.front() != '2' && host.front() != '3')) {
            return false;
        }
        bool validChars = std::all_of(host.begin(), host.end(), [](unsigned char c) {
            return std::isxdigit(c) || c == ':';
        });
        if (!validChars) return false;
        if (host.starts_with("2001:db8:") || host.starts_with("2001:0db8:")) return false;
        return true;
    }

    // numeric hosts: reject single-integer IPv4 and octal forms.
    if (std::all_of(host.begin(), host.end(), [](unsigned char c) {
            return std::isdigit(c) || c == '.';
        })) {
        size_t parts = 0;
        bool leadingZero = false;
        size_t start = 0;
        while (start <= host.size()) {
            auto dot = host.find('.', start);
            auto end = dot == std::string::npos ? host.size() : dot;
            if (end == start) return false;
            ++parts;
            if (end - start > 1 && host[start] == '0') leadingZero = true;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        if (parts != 4 || leadingZero) return false;
    }
    if (host.starts_with("0x") || host.starts_with("0X")) return false;

    if (host == "localhost" || host == "127.0.0.1" || host == "0.0.0.0") {
        return false;
    }
    if (host.starts_with("127.") || host.starts_with("0.")) return false;
    if (host.starts_with("10.") || host.starts_with("192.168.") || host.starts_with("169.254.")) {
        return false;
    }

    auto secondOctetIn = [&host](int lo, int hi) {
        size_t dot = host.find('.', 4);
        if (dot == std::string::npos) return false;
        int octet = 0;
        auto [ptr, ec] = std::from_chars(host.data() + 4, host.data() + dot, octet);
        return ec == std::errc{} && ptr == host.data() + dot && octet >= lo && octet <= hi;
    };
    if (host.starts_with("100.") && secondOctetIn(64, 127)) return false;
    if (host.starts_with("172.") && secondOctetIn(16, 31)) return false;

    return true;
}

} // namespace paimon::net
