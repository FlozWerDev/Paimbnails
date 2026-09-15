#pragma once

#include <string>
#include <string_view>
#include <vector>

// Resolves a mod repository page URL to the raw-file base its preview
// images can be fetched from.
// Thumbnail-strip idea inspired by "Mod Previews" by Alphalaneous
// (https://github.com/Alphalaneous/Mod-Previews, Geode id
// alphalaneous.mod_previews). The parsing below is an independent
// implementation for Paimbnails; only the public conventions it
// interoperates with are reused: the `previews/preview-<n>.png` path
// inside mod repos and the `main`-then-`master` default-branch probe
// (uncopyrightable interop facts). Preview images belong to each mod's
// own repository authors and are only displayed, never redistributed.
// No endorsement by the original author. See THIRD-PARTY-NOTICES.md.

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
// segments, a leading "www." and one trailing ".git".
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
    if (!segmentsOut.empty() && segmentsOut.back() == ".git") segmentsOut.pop_back();
    while (!segmentsOut.empty() && segmentsOut.back().empty()) segmentsOut.pop_back();
    return segmentsOut.size() >= 2;
}

inline std::string joinSegments(std::vector<std::string> const& segs, size_t from = 0) {
    std::string out;
    for (size_t i = from; i < segs.size(); i++) {
        if (i != from) out += '/';
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
