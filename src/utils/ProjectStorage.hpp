#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace paimon::storage {

// persistent IDs are single portable path components, never display names.
inline bool isSafeProjectId(std::string_view id) {
    if (id.empty() || id.size() > 200 || id == "." || id == ".." || id.back() == '.') return false;
    for (unsigned char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    }
    std::string stem(id.substr(0, id.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    return !(stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
             stem[3] >= '1' && stem[3] <= '9');
}

inline std::string projectIdKey(std::string_view id) {
    std::string key(id);
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    });
#endif
    return key;
}

inline std::optional<std::filesystem::path> resolveProjectDirectory(
    std::filesystem::path const& root, std::string_view id) {
    if (!isSafeProjectId(id)) return std::nullopt;
    std::error_code ec;
    auto absolute = std::filesystem::absolute(root, ec);
    if (ec) return std::nullopt;
    auto base = std::filesystem::weakly_canonical(absolute, ec);
    if (ec) return std::nullopt;
    auto candidate = base / std::string(id);
    auto status = std::filesystem::symlink_status(candidate, ec);
    // a missing leaf is already confined by its validated single component.
    // canonicalizing a nonexistent child can fail on otherwise writable Windows temp roots.
    if (ec == std::errc::no_such_file_or_directory) return candidate;
    if (ec || std::filesystem::is_symlink(status)) return std::nullopt;
    ec.clear();
    auto resolved = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || resolved.parent_path() != base) return std::nullopt;
    return candidate;
}

// non-throwing: validation happens inside the stores, so an invalid or
// unconfined id resolves to a confined quarantine leaf instead of unwinding
// through the mod's DLL boundary. Callers treat it as a missing project.
inline std::filesystem::path projectDirectory(
    std::filesystem::path const& root, std::string_view id) {
    if (auto path = resolveProjectDirectory(root, id)) return *path;
    std::error_code ec;
    auto absolute = std::filesystem::absolute(root, ec);
    return (ec ? root : absolute) / "__invalid-id__";
}

inline std::string newProjectId() {
    std::random_device random;
    constexpr char hex[] = "0123456789abcdef";
    std::string id = "p-";
    for (int word = 0; word < 4; ++word) {
        auto value = static_cast<std::uint32_t>(random());
        for (int digit = 0; digit < 8; ++digit) {
            id += hex[value & 15u];
            value >>= 4;
        }
    }
    return id;
}

// create_directory claims the ID without overwriting an existing slot or case alias.
inline std::filesystem::path reserveProjectDirectory(
    std::filesystem::path const& root, std::error_code& ec) {
    ec.clear();
    std::filesystem::create_directories(root, ec);
    if (ec) return {};
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto id = newProjectId();
        auto path = resolveProjectDirectory(root, id);
        if (!path) continue;
        if (std::filesystem::create_directory(*path, ec)) return *path;
        if (ec && ec != std::errc::file_exists) return {};
        ec.clear();
    }
    ec = std::make_error_code(std::errc::file_exists);
    return {};
}

// portable path-component character: letter, digit, '-', '_'
// (and '.', when `allowDot`). Shared by the sanitize loops in this file,
// LocalAssetStore and GDRobTopCache.
inline bool isPortableChar(unsigned char c, bool allowDot = false) {
    return std::isalnum(c) || c == '-' || c == '_' || (allowDot && c == '.');
}

// display names mapped to a single portable path component; shared by the
// icon-maker and texture-studio slot layouts (identical rules by design).
inline std::string sanitizeFilename(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (isPortableChar(uc, true)) {
            out += c;
        } else {
            out += '_';
        }
    }
    if (out.empty()) out = "_unnamed";
    // cap under the windows 255-char path-component limit.
    if (out.size() > 200) out.resize(200);
    return out;
}

} // namespace paimon::storage
