#pragma once
//
// TextureProject.hpp - Data model for one user "slot" in the texture studio.
//
// A TextureProject captures everything needed to (re)build a pack:
//   - which sheets to process,
//   - which colors to use,
//   - which sprites have manual overrides (only the index — the actual
//     mask bytes live in separate .bin files via ManualOverrideStore),
//   - timestamps for sorting in the slot list UI,
//   - per-sprite auto-detection cache index (entries themselves go in
//     a separate binary blob to keep project.json fast to parse).
//
// Why split heavy data out of project.json:
//   project.json is the file the UI reads to populate the slot grid. We
//   want it to load instantly even for slots with hundreds of overrides.
//   Keeping project.json under a few KB lets us load all slots in one
//   sweep on first open.
//

#include "../engine/PackExporterTypes.hpp"

#include <Geode/cocos/include/ccTypes.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// Reference to a sheet that this project pulls from. Stored as paths so
// re-opening the slot still works even if GD is reinstalled to a new path
// (the UI re-resolves via GdResourcesLocator and warns on mismatch).
struct ProjectSheetRef {
    std::string baseName;        // "GJ_GameSheet01"
    std::string qualitySuffix;   // "-uhd"
    std::string sourcePlistPath; // string-form for matjson round-trip
    std::string sourcePngPath;
};

// Index of one manual override (the mask bytes live in a .bin file).
struct ManualOverrideRef {
    std::string spriteName;      // canonical name, used for filename
    int  width  = 0;
    int  height = 0;
    int  version = 1;
    std::int64_t modifiedAt = 0; // unix ms; lets us purge stale overrides
};

// Index of one cached cluster set (the cluster details live in auto.bin).
struct AutoCacheRef {
    std::string  spriteName;
    std::uint64_t spriteHash = 0; // FNV-1a of the source sprite's RGBA bytes
    int          clusterCount = 0;
};

// One full slot.
struct TextureProject {
    int schemaVersion = 1;

    // ── Identity ───────────────────────────────────────────────────────
    std::string id;          // e.g. "paimbnails.texture_studio.mi_pack_rosa"
    std::string name;        // user-facing, "Mi Pack Rosa"
    std::string author;      // optional; shown on pack.json
    std::int64_t createdAt  = 0;  // unix ms
    std::int64_t modifiedAt = 0;

    // ── Source sheets ──────────────────────────────────────────────────
    std::vector<ProjectSheetRef> sheets;

    // ── Recoloring config ──────────────────────────────────────────────
    cocos2d::ccColor3B color1{149, 226, 3};
    cocos2d::ccColor3B color2{28, 233, 255};
    cocos2d::ccColor3B colorGlow{255, 255, 255};
    int  brightness = 160;

    // ── Toggles ────────────────────────────────────────────────────────
    bool includeMediumPort       = false;
    bool alternativeGlowOverlay  = false;
    bool transparentLists        = false;
    bool colorGradientBg         = false;
    bool colorMainMenu           = false;

    // ── Indexed heavy data (actual blobs on disk) ──────────────────────
    std::map<std::string, ManualOverrideRef> overrides;  // spriteName → ref
    std::map<std::string, AutoCacheRef>      autoCache;  // spriteName → ref

    // ── Last build state ───────────────────────────────────────────────
    bool         hasBuiltOnce  = false;
    std::int64_t lastBuiltAt   = 0;
    std::string  lastZipRelPath; // relative to slotDir, e.g. "output/pack.zip"

    // ── Helpers ────────────────────────────────────────────────────────

    // Convert this project's recoloring config + sheets into a
    // PackExportConfig ready for PackExporter. The caller fills the
    // actual SheetSelection paths from current GD resources.
    PackExportConfig toExportConfig() const;
};

// Generate a unix-ms timestamp (clock_gettime / system_clock::now()).
std::int64_t nowUnixMs();

}  // namespace paimon::texture_studio
