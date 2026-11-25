#pragma once

#include <string>

namespace paimon::death_effects {

// pure formatting helpers shared by the catalogue, the manager and the UI.
// header-only so the portable regression suite can exercise them.

inline std::string upperAscii(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    }
    return value;
}

// "by" + "4.0" -> "CC BY 4.0"; "cc0" + "1.0" -> "CC0 1.0".
inline std::string licenseLabel(std::string const& license, std::string const& version) {
    auto upper = upperAscii(license);
    if (upper.empty()) return "";
    if (upper == "CC0") {
        return version.empty() ? "CC0" : "CC0 " + version;
    }
    return version.empty() ? "CC " + upper : "CC " + upper + " " + version;
}

// keeps a downloaded title safe as a file name: letters, digits, '-' and
// '_', words joined by single dashes, at most 60 characters.
inline std::string sanitizeFileStem(std::string const& name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char ch : name) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (ch == ' ' && !out.empty() && out.back() != '-') {
            out.push_back('-');
        }
        if (out.size() >= 60) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("openverse-sfx") : out;
}

} // namespace paimon::death_effects
