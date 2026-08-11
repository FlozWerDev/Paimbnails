#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Mirror of the Firestore model behind https://modly.web.app (project "mods-web").
// The site keeps logos, previews, avatars and banners as base64 data-URLs inside
// the documents; the worker strips them out and re-serves them as plain PNG URLs,
// so everything here is small enough to hold in memory.

namespace paimon::compat_mods {

struct ModlyComment {
    std::string id;
    std::string text;
    std::string authorUid;
    std::string authorName;
    int64_t date = 0;   // unix seconds, 0 when the server timestamp was still pending
};

struct ModlyUser {
    std::string uid;
    std::string name;
    std::string description;
    std::string rank;               // "rojo" | "verde" | "" (see badgeRango in app.js)
    std::vector<std::string> tags;  // "Desarrollador", "Disenador", ...
    bool verified = false;
    bool hasPhoto = false;
    bool hasBanner = false;
};

struct ModlyMod {
    std::string id;
    std::string name;
    std::string description;
    std::string version;
    std::string link;       // download target: GitHub release or Google Drive
    std::string gdps;       // non-empty when the mod targets a specific GDPS
    std::string discord;
    std::string kofi;
    std::string repo;
    std::string type;       // "mod" | "pack"
    std::string state;      // "" | "alpha" | "beta"
    std::string authorUid;
    std::string authorName;
    int downloads = 0;
    int64_t date = 0;
    int previewCount = 0;
    bool hasLogo = false;

    bool isPack() const { return type == "pack"; }
};

} // namespace paimon::compat_mods
