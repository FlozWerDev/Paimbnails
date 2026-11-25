#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <chrono>
#include <mutex>

namespace paimon::wallpaper {

enum class WallpaperType {
    All,
    Video,
    Web,
    Scene,
    Gif,
    Image,
    Procedural,
    Unsupported
};

struct WallpaperItem {
    std::string id;              // Folder name, Workshop ID or path hash
    std::string title;           // Friendly display title
    std::string author;          // Creator (credits & attribution)
    std::string description;     // Description or notes
    WallpaperType type = WallpaperType::Unsupported;
    std::string typeStr;         // "video", "web", "scene", "gif", "image"
    std::filesystem::path rootPath;
    std::filesystem::path entryFile;
    std::filesystem::path previewPath;
    std::vector<std::string> tags;
    uint64_t workshopId = 0;     // Steam Workshop ID (for official steam links)
    bool isAudioReactive = false;
    // resolved by the scan worker: a built-in renderer can play this item.
    bool playable = false;
    // a scene drawn through a 3D camera; the 2D scene renderer cannot show it.
    bool scene3D = false;
    std::string contentRating = "Everyone";
};

class WallpaperEngineManager {
public:
    // snapshot of the user settings a scan needs, taken on the main thread so
    // the scan itself can run on a worker without touching saved values.
    struct ScanRequest {
        std::vector<std::filesystem::path> importedFolders;
        std::filesystem::path customScanDir;
        bool bypassCache = false;
    };

    static WallpaperEngineManager& get();

    // cross-platform portable directory (works on Android, macOS, Windows)
    std::filesystem::path getPortableWallpaperDir() const;

    // main thread only: reads saved values.
    ScanRequest makeScanRequest() const;

    // filesystem walk. Safe off the main thread; results are cached briefly so
    // reopening the picker does not rescan the whole Steam workshop again.
    std::vector<WallpaperItem> scanWallpapers(ScanRequest const& request);
    std::vector<WallpaperItem> scanWallpapers();

    // validates project.json metadata, entry files, and blocks executable payloads.
    std::optional<WallpaperItem> inspectWallpaperFolder(std::filesystem::path const& dir);
    std::optional<WallpaperItem> importWallpaperFolder(std::filesystem::path const& dir);

    std::vector<WallpaperItem> filterByType(std::vector<WallpaperItem> const& list, WallpaperType filter);

    // workshop & creator links (official Steam deep links and web fallbacks)
    std::string getWorkshopWebUrl(WallpaperItem const& item) const;
    std::string getWorkshopSteamUri(WallpaperItem const& item) const;
    void openWorkshopPage(WallpaperItem const& item) const;

    bool applyWallpaper(std::string const& layerKey, WallpaperItem const& item);

    // wallpaper Engine's read-only assets folder, when it is installed. Scene
    // presets reference particle textures and solid layers stored there.
    // thread-safe; resolved once.
    std::filesystem::path wallpaperEngineAssetsDir() const;

    static char const* typeToString(WallpaperType type);
    static WallpaperType stringToType(std::string const& str);

private:
    WallpaperEngineManager() = default;
    std::vector<std::filesystem::path> findSteamLibraries() const;
    std::vector<std::filesystem::path> collectScanDirectories(ScanRequest const& request) const;

    mutable std::mutex m_scanCacheMutex;
    std::vector<WallpaperItem> m_scanCache;
    std::chrono::steady_clock::time_point m_scanCacheTime{};
    bool m_scanCacheValid = false;
};

} // namespace paimon::wallpaper
