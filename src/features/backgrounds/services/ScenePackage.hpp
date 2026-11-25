#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace paimon::wallpaper {

// wallpaper Engine's PKGV archive has an uncompressed directory followed by
// concatenated file contents. No archive entry is ever extracted to disk.
class ScenePackage {
public:
    struct Entry { uint64_t offset = 0; uint32_t size = 0; };

    static std::string normalizeName(std::string_view name) {
        std::string s(name);
        for (char& c : s) {
            if (c == '\\') c = '/';
        }
        return s;
    }

    static bool safeName(std::string_view name) {
        if (name.empty()) return false;
        auto norm = normalizeName(name);
        if (norm.front() == '/' || norm.back() == '/' ||
            norm.find(':') != norm.npos || norm.find('\0') != norm.npos) return false;
        size_t start = 0;
        while (start < norm.size()) {
            auto const end = norm.find('/', start);
            auto const component = norm.substr(start, end == norm.npos ? norm.npos : end - start);
            if (component.empty() || component == "." || component == "..") return false;
            if (end == norm.npos) break;
            start = end + 1;
        }
        return true;
    }

    static std::optional<ScenePackage> open(std::filesystem::path const& inputPath) {
        ScenePackage pkg;
        std::filesystem::path root = inputPath;
        std::error_code ec;

        if (std::filesystem::is_regular_file(root, ec)) {
            auto filename = root.filename().string();
            for (char& c : filename) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (filename == "scene.pkg") {
                pkg.m_root = root.parent_path();
                pkg.m_archive = root;
            } else {
                root = root.parent_path();
            }
        }

        if (pkg.m_archive.empty()) {
            pkg.m_root = root;
            auto const archive = root / "scene.pkg";
            if (!std::filesystem::is_regular_file(archive, ec)) {
                if (std::filesystem::is_regular_file(root / "scene.json", ec)) return pkg;
                return std::nullopt;
            }
            pkg.m_archive = archive;
        }

        auto const length = std::filesystem::file_size(pkg.m_archive, ec);
        if (ec || length < 20 || length > 8ull * 1024 * 1024 * 1024) return std::nullopt;
        std::ifstream file(pkg.m_archive, std::ios::binary);
        if (!file) return std::nullopt;

        auto read32 = [&file](uint32_t& value) {
            unsigned char bytes[4]{};
            if (!file.read(reinterpret_cast<char*>(bytes), 4)) return false;
            value = uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8 |
                uint32_t(bytes[2]) << 16 | uint32_t(bytes[3]) << 24;
            return true;
        };
        uint32_t versionLength = 0, count = 0;
        if (!read32(versionLength) || versionLength != 8) return std::nullopt;
        char version[8]{};
        if (!file.read(version, 8) || std::string_view(version, 4) != "PKGV" ||
            !read32(count) || count == 0 || count > 100000) return std::nullopt;
        struct RelativeEntry { std::string name; uint32_t offset; uint32_t size; };
        std::vector<RelativeEntry> entries;
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t nameLength = 0, offset = 0, size = 0;
            if (!read32(nameLength) || !nameLength || nameLength > 4096) return std::nullopt;
            std::string rawName(nameLength, '\0');
            if (!file.read(rawName.data(), nameLength)) return std::nullopt;
            std::string name = normalizeName(rawName);
            if (!safeName(name) || !read32(offset) || !read32(size)) return std::nullopt;
            entries.push_back({std::move(name), offset, size});
        }
        auto const payload = file.tellg();
        if (payload < 0 || static_cast<uint64_t>(payload) > length) return std::nullopt;
        for (auto& entry : entries) {
            uint64_t const start = static_cast<uint64_t>(payload) + entry.offset;
            if (start > length || entry.size > length - start) return std::nullopt;
            std::string lower = entry.name;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            pkg.m_lowerEntries.emplace(lower, entry.name);
            if (!pkg.m_entries.emplace(std::move(entry.name), Entry{start, entry.size}).second)
                return std::nullopt;
        }
        if (!pkg.contains("scene.json")) return std::nullopt;
        return pkg;
    }

    bool contains(std::string const& rawName) const {
        if (!safeName(rawName)) return false;
        auto const name = normalizeName(rawName);
        if (!m_archive.empty()) {
            if (m_entries.contains(name)) return true;
            std::string lower = name;
            for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (m_lowerEntries.contains(lower)) return true;
        } else {
            std::error_code ec;
            if (std::filesystem::is_regular_file(m_root / pathOf(name), ec)) return true;
        }
        return fallbackFile(name).has_value();
    }

    // wallpaper Engine's own assets folder (read-only): presets reference
    // particle textures, solid layers and fonts that are not in the archive.
    void setFallbackRoot(std::filesystem::path root) { m_fallback = std::move(root); }

    // stable identity of the package on disk (cache keys).
    std::filesystem::path const& location() const { return m_archive.empty() ? m_root : m_archive; }

    std::vector<uint8_t> read(std::string const& rawName, size_t maxBytes = 128 * 1024 * 1024) const {
        if (!safeName(rawName)) return {};
        auto const name = normalizeName(rawName);
        std::ifstream file;
        uint64_t size = 0;
        bool found = false;
        if (m_archive.empty()) {
            auto const path = m_root / pathOf(name);
            std::error_code ec;
            if (std::filesystem::is_regular_file(path, ec)) {
                size = std::filesystem::file_size(path, ec);
                if (ec) return {};
                file.open(path, std::ios::binary);
                found = true;
            }
        } else {
            auto it = m_entries.find(name);
            if (it == m_entries.end()) {
                std::string lower = name;
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                auto lowerIt = m_lowerEntries.find(lower);
                if (lowerIt != m_lowerEntries.end()) {
                    it = m_entries.find(lowerIt->second);
                }
            }
            if (it != m_entries.end()) {
                size = it->second.size;
                file.open(m_archive, std::ios::binary);
                if (file) file.seekg(static_cast<std::streamoff>(it->second.offset));
                found = true;
            }
        }
        if (!found) {
            auto const fallback = fallbackFile(name);
            if (!fallback) return {};
            std::error_code ec;
            size = std::filesystem::file_size(*fallback, ec);
            if (ec) return {};
            file.open(*fallback, std::ios::binary);
        }
        if (!file || size == 0 || size > maxBytes) return {};
        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) return {};
        return data;
    }

private:
    static std::filesystem::path pathOf(std::string const& name) {
        return std::filesystem::path(std::u8string(name.begin(), name.end()));
    }

    std::optional<std::filesystem::path> fallbackFile(std::string const& name) const {
        if (m_fallback.empty()) return std::nullopt;
        std::error_code ec;
        auto path = m_fallback / pathOf(name);
        if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
        return path;
    }

    std::filesystem::path m_root;
    std::filesystem::path m_archive;
    std::filesystem::path m_fallback;
    std::unordered_map<std::string, Entry> m_entries;
    std::unordered_map<std::string, std::string> m_lowerEntries;
};

} // namespace paimon::wallpaper
