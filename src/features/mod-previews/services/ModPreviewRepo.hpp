#pragma once

#include <string>
#include <string_view>
#include <vector>

// Resolves a mod repository page URL to the raw-file base its preview
// images can be fetched from. Strip idea compatible with "Mod Previews" by
// Alphalaneous; parsing below is our own (see THIRD-PARTY-NOTICES.md).

namespace paimon::mod_previews {

// Raw-file base for one repository, branch still unresolved.
struct PreviewSource {
    bool ok = false;
    // Host-specific raw base WITHOUT the branch, e.g.
    // "https://raw.githubusercontent.com/<owner>/<repo>".
    // Manifest: <assetBase>/<branch>/mod.json
    // Thumbs:   <assetBase>/<branch>/previews/preview-<n>.png
    std::string assetBase;
};

// Split a URL into lowercase host + path segments, dropping empty
// segments, a leading "www." and a trailing ".git" suffix.
inline bool parseHttpUrl(std::string_view url, std::string& hostOut,
                         std::vector<std::string>& segmentsOut) {
    auto scheme = url.find("://");
    if (scheme == std::string_view::npos) return false;
    std::string_view rest = url.substr(scheme + 3);

    auto slash = rest.find('/');
    std::string_view host = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    std::string_view path = (slash == std::string_view::npos) ? "" : rest.substr(slash + 1);

    hostOut.clear();
    for (char c : host) hostOut += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (hostOut.rfind("www.", 0) == 0) hostOut.erase(0, 4);
    if (hostOut.empty()) return false;

    segmentsOut.clear();
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty() && cur != ".") segmentsOut.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty() && cur != ".") segmentsOut.push_back(cur);
    // The last segment may carry the clone suffix ("repo.git"); a bare
    // ".git" segment only happens for degenerate URLs, drop it too.
    if (!segmentsOut.empty()) {
        auto& last = segmentsOut.back();
        if (last == ".git") segmentsOut.pop_back();
        else if (last.size() > 4 && last.ends_with(".git")) last.erase(last.size() - 4);
    }
    return segmentsOut.size() >= 2;
}

inline std::string joinSegments(std::vector<std::string> const& segs) {
    std::string out;
    for (size_t i = 0; i < segs.size(); i++) {
        if (i != 0) out += '/';
        out += segs[i];
    }
    return out;
}

// Maps a repo page URL to its raw asset base. Table-driven per host so
// adding a forge means adding a row, not a code path.
inline PreviewSource resolvePreviewSource(std::string const& pageUrl) {
    std::string host;
    std::vector<std::string> segs;
    if (!parseHttpUrl(pageUrl, host, segs)) return {};

    PreviewSource src;
    if (host == "github.com") {
        src.assetBase = "https://raw.githubusercontent.com/" + joinSegments(segs);
        src.ok = true;
    } else if (host == "codeberg.org") {
        src.assetBase = "https://codeberg.org/" + joinSegments(segs) + "/raw/branch";
        src.ok = true;
    }
    return src;
}

} // namespace paimon::mod_previews
