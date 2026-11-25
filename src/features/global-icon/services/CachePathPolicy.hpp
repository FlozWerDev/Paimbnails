#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>

namespace paimon::globalicon {

inline bool safeCacheFilename(std::string_view name) {
    if (name.empty() || name.size() > 200 || name == "." || name == ".." ||
        name.back() == '.' || name.back() == ' ') return false;
    for (unsigned char c : name) {
        if (c < 32 || std::string_view("/\\:<>\"|?*").find(c) != std::string_view::npos) return false;
    }
    std::string stem(name.substr(0, name.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
        return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
    });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    if (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
        stem[3] >= '1' && stem[3] <= '9') return false;
    return true;
}

// resolve existing symlinks as well as lexical '..' before checking containment.
inline bool isInsideCache(std::filesystem::path const& root, std::filesystem::path const& path) {
    std::error_code ec;
    auto base = std::filesystem::weakly_canonical(root, ec);
    if (ec) return false;
    auto target = std::filesystem::weakly_canonical(path, ec);
    if (ec) return false;
    auto b = base.begin();
    auto t = target.begin();
    for (; b != base.end(); ++b, ++t) {
        if (t == target.end() || *b != *t) return false;
    }
    return t != target.end();
}

} // namespace paimon::globalicon
