#pragma once
//
// PackGenAssets.hpp - Downloads & caches the asset pack hosted at
// https://packgenweb.pages.dev/pack/ (made by Asterveila for PackGen).
//
// PackGen's recoloring pipeline relies on hand-painted overlay PNGs:
//
//     <name>.png            (base sprite, with outline already in place)
//     <name>_OVERLAY1.png   (greyscale mask: pixels that receive Color1)
//     <name>_OVERLAY2.png   (greyscale mask: pixels that receive Color2)
//     <name>_GLOWOVERLAY.png (greyscale mask: pixels that receive Glow)
//
// The web app fetches each file at generation time. We do the same and
// cache the bytes to disk so subsequent generations don't re-download.
//
// All asset URLs are derived from the manifest at:
//     https://packgenweb.pages.dev/pack/manifest.json
//
// Layout on disk:
//     <Mod::get()->getSaveDir()>/texture-studio/packgen-cache/
//         manifest.json
//         GJ_button_01-uhd.png
//         GJ_button_01-uhd_OVERLAY1.png
//         GJ_button_01-uhd_OVERLAY2.png
//         GJ_GameSheet03-uhd.plist
//         GJ_GameSheet03-uhd.png
//         ...
//

#include <Geode/Geode.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// One entry from the PackGen manifest. We split the original "files"
// flat list into (relativePath, hasOverlay1, hasOverlay2, hasGlow) so
// the caller knows what to fetch without firing extra HEAD requests.
struct PackGenManifestEntry {
    std::string relativePath;       // e.g. "GJ_button_01-uhd.png"
    bool        hasOverlay1 = false;
    bool        hasOverlay2 = false;
    bool        hasGlow     = false;
    bool        isPlist     = false;  // true → must be paired with a .png sheet
    bool        isPng       = false;  // true → may have overlays / be a sheet
};

// Result of resolving the manifest. The caller iterates `entries`.
struct PackGenManifest {
    std::vector<PackGenManifestEntry> entries;

    // List of files that must NOT be scaled in the medium port (from
    // PackGen's noscaling.json, optional).
    std::vector<std::string> noScalingFiles;
};

// Progress callback signature: (currentIndex, totalFiles, currentFileLabel).
using PackGenProgressCallback = std::function<void(int, int, std::string const&)>;

class PackGenAssets final {
public:
    // Get the singleton.
    static PackGenAssets& get();

    // Root URL of the upstream PackGen asset host. Configurable in case
    // the user wants to point at a mirror.
    std::string baseUrl() const { return m_baseUrl; }
    void setBaseUrl(std::string url);

    // Local cache directory. Created on demand.
    std::filesystem::path cacheDir() const;

    // Whether the manifest has been resolved (downloaded or loaded from
    // the local cache).
    bool isManifestLoaded() const { return !m_manifest.entries.empty(); }

    // Expose the manifest for callers (PackExporter iterates it).
    PackGenManifest const& manifest() const { return m_manifest; }

    // Download (or refresh) the manifest. Synchronous — call from a
    // background thread. Returns Ok on success, Err with a message on
    // failure (network error, JSON malformed, etc.).
    geode::Result<> ensureManifest(bool forceRefresh = false);

    // Make sure a single file from the manifest is on disk. Downloads if
    // missing. Synchronous — call from a background thread.
    geode::Result<std::filesystem::path> ensureFile(std::string const& relativePath);

    // Convenience: resolve the local path for a relative file regardless
    // of whether it has been downloaded yet. Caller must call ensureFile
    // before reading.
    std::filesystem::path localPathFor(std::string const& relativePath) const;

    // Bulk-download all manifest files. Reports progress through `progress`
    // (called from the calling thread). Returns the number of files that
    // were already cached vs newly downloaded.
    struct PrefetchResult {
        int alreadyCached = 0;
        int downloaded    = 0;
        int failed        = 0;
        std::vector<std::string> failedPaths;
    };
    PrefetchResult prefetchAll(PackGenProgressCallback progress);

private:
    PackGenAssets();

    std::string                m_baseUrl;
    PackGenManifest            m_manifest;

    // Internal helpers.
    geode::Result<> parseManifestJson(std::string const& jsonText);
    geode::Result<std::vector<std::uint8_t>> downloadBytes(std::string const& url);
    geode::Result<> writeBytes(std::filesystem::path const& path,
                               std::vector<std::uint8_t> const& bytes);
};

}  // namespace paimon::texture_studio
