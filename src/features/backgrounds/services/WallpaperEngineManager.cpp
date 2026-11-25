#include "../../../utils/FilesystemPath.hpp"
#include "WallpaperEngineManager.hpp"
#include "LayerBackgroundManager.hpp"
#include "WallpaperPlayback.hpp"
#include "WebWallpaperRenderer.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/string.hpp>
#include <matjson.hpp>
#include <algorithm>
#include <cctype>

#ifdef GEODE_IS_WINDOWS
#include <windows.h>
#endif

namespace paimon::wallpaper {

namespace {
// 2D scenes declare an orthogonal projection; 3D ones leave it null and use
// the camera. A text probe avoids parsing large scene files during a scan.
bool isPerspectiveScene(WallpaperItem const& item) {
    if (item.type != WallpaperType::Scene) return false;
    auto package = ScenePackage::open(item.rootPath);
    if (!package) return false;
    auto const bytes = package->read("scene.json", 64ull * 1024 * 1024);
    if (bytes.empty()) return false;
    std::string_view const text(reinterpret_cast<char const*>(bytes.data()), bytes.size());
    auto const key = text.find("\"orthogonalprojection\"");
    if (key == std::string_view::npos) return text.find("\"general\"") != std::string_view::npos;
    auto value = text.find_first_not_of(" \t\r\n:", key + 22);
    return value != std::string_view::npos && text.substr(value, 4) == "null";
}
} // namespace

WallpaperEngineManager& WallpaperEngineManager::get() {
    static WallpaperEngineManager instance;
    return instance;
}

std::filesystem::path WallpaperEngineManager::getPortableWallpaperDir() const {
    auto dir = paimon::paths::toStdPath(geode::Mod::get()->getSaveDir()) / "wallpapers";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::vector<std::filesystem::path> WallpaperEngineManager::collectScanDirectories(ScanRequest const& request) const {
    std::vector<std::filesystem::path> dirs;
    std::error_code ec;

    // 1. Cross-platform user storage directory (Android, macOS, Windows)
    auto portable = getPortableWallpaperDir();
    if (std::filesystem::exists(portable, ec)) {
        dirs.push_back(portable);
    }

    // 2. Mod runtime folder (bundled assets or user-dragged wallpapers)
    auto runtimeDir = paimon::paths::toStdPath(geode::Mod::get()->getResourcesDir()) / "wallpapers";
    if (std::filesystem::exists(runtimeDir, ec)) {
        dirs.push_back(runtimeDir);
    }

    // 3. User configured custom scan directory
    if (!request.customScanDir.empty()) {
        if (std::filesystem::exists(request.customScanDir, ec) && std::filesystem::is_directory(request.customScanDir, ec)) {
            dirs.push_back(request.customScanDir);
        }
    }

    // 4. Steam Workshop & Wallpaper Engine directories (Windows/Steam platform)
    auto steamLibs = findSteamLibraries();
    for (auto const& lib : steamLibs) {
        // workshop folder (appid 431960 is wallpaper engine)
        auto wsPath = lib / "steamapps" / "workshop" / "content" / "431960";
        if (std::filesystem::exists(wsPath, ec) && std::filesystem::is_directory(wsPath, ec)) {
            dirs.push_back(wsPath);
        }
        // default bundled projects
        auto defProjects = lib / "steamapps" / "common" / "wallpaper_engine" / "projects" / "defaultprojects";
        if (std::filesystem::exists(defProjects, ec) && std::filesystem::is_directory(defProjects, ec)) {
            dirs.push_back(defProjects);
        }
        // user created projects in wallpaper engine
        auto myProjects = lib / "steamapps" / "common" / "wallpaper_engine" / "projects" / "myprojects";
        if (std::filesystem::exists(myProjects, ec) && std::filesystem::is_directory(myProjects, ec)) {
            dirs.push_back(myProjects);
        }
    }

    return dirs;
}

std::vector<std::filesystem::path> WallpaperEngineManager::findSteamLibraries() const {
    std::vector<std::filesystem::path> libraries;
#ifdef GEODE_IS_WINDOWS
    std::error_code ec;

    std::vector<std::filesystem::path> baseSteamCandidates = {
        "C:/Program Files (x86)/Steam",
        "C:/Program Files/Steam",
        "D:/Steam",
        "D:/SteamLibrary",
        "E:/Steam",
        "E:/SteamLibrary",
        "F:/Steam",
        "F:/SteamLibrary"
    };

    // query registry for exact steam install path if available
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[MAX_PATH] = {0};
        DWORD bufferSize = sizeof(buffer);
        if (RegQueryValueExA(hKey, "SteamPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &bufferSize) == ERROR_SUCCESS) {
            std::filesystem::path regPath(buffer);
            if (std::filesystem::exists(regPath, ec)) {
                baseSteamCandidates.insert(baseSteamCandidates.begin(), regPath);
            }
        }
        RegCloseKey(hKey);
    }

    for (auto const& candidate : baseSteamCandidates) {
        if (!std::filesystem::exists(candidate, ec) || !std::filesystem::is_directory(candidate, ec)) continue;

        if (std::find(libraries.begin(), libraries.end(), candidate) == libraries.end()) {
            libraries.push_back(candidate);
        }

        auto vdfPath = candidate / "steamapps" / "libraryfolders.vdf";
        if (!std::filesystem::exists(vdfPath, ec)) continue;

        auto vdfRes = geode::utils::file::readString(paimon::paths::toSdkPath(vdfPath));
        if (!vdfRes.isOk()) continue;

        std::string const& vdfContent = vdfRes.unwrap();
        size_t pos = 0;
        while ((pos = vdfContent.find("\"path\"", pos)) != std::string::npos) {
            pos += 6;
            while (pos < vdfContent.size() && (vdfContent[pos] == ' ' || vdfContent[pos] == '\t' || vdfContent[pos] == '\"')) {
                ++pos;
            }
            size_t endQuote = vdfContent.find('\"', pos);
            if (endQuote == std::string::npos) break;

            std::string libPathStr = vdfContent.substr(pos, endQuote - pos);
            std::string normalized;
            for (size_t i = 0; i < libPathStr.size(); ++i) {
                if (libPathStr[i] == '\\' && i + 1 < libPathStr.size() && libPathStr[i+1] == '\\') {
                    normalized += '/';
                    ++i;
                } else if (libPathStr[i] == '\\') {
                    normalized += '/';
                } else {
                    normalized += libPathStr[i];
                }
            }

            std::filesystem::path libP(normalized);
            if (std::filesystem::exists(libP, ec) && std::filesystem::is_directory(libP, ec)) {
                if (std::find(libraries.begin(), libraries.end(), libP) == libraries.end()) {
                    libraries.push_back(libP);
                }
            }
            pos = endQuote + 1;
        }
    }
#endif
    return libraries;
}

std::filesystem::path WallpaperEngineManager::wallpaperEngineAssetsDir() const {
    static std::once_flag once;
    static std::filesystem::path assets;
    std::call_once(once, [this] {
        std::error_code ec;
        for (auto const& lib : findSteamLibraries()) {
            auto candidate = lib / "steamapps" / "common" / "wallpaper_engine" / "assets";
            if (std::filesystem::is_directory(candidate, ec)) {
                assets = candidate;
                return;
            }
        }
    });
    return assets;
}

std::optional<WallpaperItem> WallpaperEngineManager::inspectWallpaperFolder(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return std::nullopt;
    }

    WallpaperItem item;
    item.rootPath = dir;
    item.id = dir.filename().string();
    item.title = item.id;
    item.author = "Desconocido";

    // check if directory name is numeric (standard steam workshop publishedfileid)
    bool isNumeric = !item.id.empty();
    for (char c : item.id) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            isNumeric = false;
            break;
        }
    }
    if (isNumeric) {
        try {
            item.workshopId = std::stoull(item.id);
        } catch (...) {}
    }

    auto projectPath = dir / "project.json";
    if (std::filesystem::exists(projectPath, ec)) {
        auto jsonRead = geode::utils::file::readString(paimon::paths::toSdkPath(projectPath));
        if (jsonRead.isOk()) {
            auto parsed = matjson::parse(jsonRead.unwrap());
            if (parsed.isOk() && parsed.unwrap().isObject()) {
                auto const& obj = parsed.unwrap();

                if (obj.contains("title")) {
                    item.title = obj["title"].asString().unwrapOr(item.title);
                }
                if (obj.contains("author")) {
                    item.author = obj["author"].asString().unwrapOr(item.author);
                }
                if (obj.contains("description")) {
                    item.description = obj["description"].asString().unwrapOr("");
                }
                if (obj.contains("type")) {
                    item.typeStr = obj["type"].asString().unwrapOr("");
                    item.type = stringToType(item.typeStr);
                }
                if (obj.contains("contentrating")) {
                    item.contentRating = obj["contentrating"].asString().unwrapOr("Everyone");
                }
                if (obj.contains("workshopid")) {
                    if (obj["workshopid"].isNumber()) {
                        item.workshopId = static_cast<uint64_t>(obj["workshopid"].asInt().unwrapOr(0));
                    } else if (obj["workshopid"].isString()) {
                        try {
                            item.workshopId = std::stoull(obj["workshopid"].asString().unwrapOr("0"));
                        } catch (...) {}
                    }
                }

                if (obj.contains("file")) {
                    std::string f = obj["file"].asString().unwrapOr("");
                    if (!f.empty()) {
                        item.entryFile = dir / paimon::assets::pathFromUtf8(f);
                    }
                }

                if (obj.contains("preview")) {
                    std::string prev = obj["preview"].asString().unwrapOr("");
                    if (!prev.empty()) {
                        auto prevCandidate = dir / paimon::assets::pathFromUtf8(prev);
                        if (std::filesystem::exists(prevCandidate, ec)) {
                            item.previewPath = prevCandidate;
                        }
                    }
                }

                if (obj.contains("tags") && obj["tags"].isArray()) {
                    for (auto const& tagVal : obj["tags"].asArray().unwrap()) {
                        auto s = tagVal.asString().unwrapOr("");
                        if (!s.empty()) item.tags.push_back(s);
                    }
                }
            }
        }
    }

    // probe common image filenames if project.json does not declare an explicit preview.
    if (item.previewPath.empty()) {
        std::vector<std::string> previewNames = {"preview.jpg", "preview.png", "preview.gif", "icon.png", "thumb.jpg"};
        for (auto const& name : previewNames) {
            auto candidate = dir / name;
            if (std::filesystem::exists(candidate, ec)) {
                item.previewPath = candidate;
                break;
            }
        }
    }

    // packed scenes may omit scene.json. keep their declared type so a preview
    // cannot be mistaken for the original wallpaper.
    if (item.type != WallpaperType::Web && item.type != WallpaperType::Scene &&
        (item.entryFile.empty() || !std::filesystem::exists(item.entryFile, ec))) {
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.is_regular_file(ec)) {
                if (entry.path().filename() == "index.html") {
                    item.entryFile = entry.path();
                    item.type = WallpaperType::Web;
                    item.typeStr = "web";
                    break;
                } else if (entry.path().filename() == "scene.pkg" || entry.path().filename() == "scene.json") {
                    item.entryFile = entry.path();
                    item.type = WallpaperType::Scene;
                    item.typeStr = "scene";
                    break;
                } else if (auto media = wallpaperMediaFile(entry.path());
                           media && media->type != WallpaperType::Image) {
                    item.entryFile = entry.path();
                    item.type = media->type;
                    item.typeStr = media->type == WallpaperType::Video ? "video" : "gif";
                    break;
                }
            }
        }
    }

    // safety check: executables are blocked for cybersecurity protection
    if (item.type == WallpaperType::Unsupported && item.typeStr == "application") {
        return std::nullopt;
    }

    // detect audio reactivity from tags and description
    std::string descLower;
    for (char c : item.description) descLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (descLower.find("audio") != std::string::npos ||
        descLower.find("reactive") != std::string::npos ||
        descLower.find("visualizer") != std::string::npos ||
        descLower.find("music") != std::string::npos) {
        item.isAudioReactive = true;
    }
    for (auto const& t : item.tags) {
        std::string tLower;
        for (char c : t) tLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tLower.find("audio") != std::string::npos || tLower.find("reactive") != std::string::npos) {
            item.isAudioReactive = true;
            break;
        }
    }

    return item;
}

std::optional<WallpaperItem> WallpaperEngineManager::importWallpaperFolder(std::filesystem::path const& dir) {
    auto item = inspectWallpaperFolder(dir);
    if (!item || !resolveWallpaperPlayback(*item) || isPerspectiveScene(*item)) return std::nullopt;
    item->playable = true;
    auto folders = geode::Mod::get()->getSavedValue<std::vector<std::string>>("wallpaper-import-folders", {});
    auto const path = paimon::paths::toUtf8(dir.lexically_normal());
    if (std::find(folders.begin(), folders.end(), path) == folders.end()) {
        folders.push_back(path);
        geode::Mod::get()->setSavedValue("wallpaper-import-folders", folders);
        if (auto saved = geode::Mod::get()->saveData(); !saved) {
            geode::log::warn("Could not persist the imported wallpaper folder: {}", saved.unwrapErr());
        }
    }
    return item;
}

WallpaperEngineManager::ScanRequest WallpaperEngineManager::makeScanRequest() const {
    ScanRequest request;

    for (auto const& folder : geode::Mod::get()->getSavedValue<std::vector<std::string>>("wallpaper-import-folders", {})) {
        request.importedFolders.push_back(paimon::assets::pathFromUtf8(folder));
    }

    auto const customScan = geode::Mod::get()->getSavedValue<std::string>("we-custom-scan-dir", "");
    if (!customScan.empty()) {
        request.customScanDir = std::filesystem::path(customScan);
    }

    return request;
}

std::vector<WallpaperItem> WallpaperEngineManager::scanWallpapers() {
    return scanWallpapers(makeScanRequest());
}

std::vector<WallpaperItem> WallpaperEngineManager::scanWallpapers(ScanRequest const& request) {
    // picker opens and filter changes reuse the last walk; the steam workshop
    // tree can hold thousands of folders and stats add up fast.
    constexpr auto kCacheTtl = std::chrono::seconds(20);

    {
        std::lock_guard lock(m_scanCacheMutex);
        if (!request.bypassCache && m_scanCacheValid &&
            std::chrono::steady_clock::now() - m_scanCacheTime < kCacheTtl) {
            return m_scanCache;
        }
    }

    std::vector<WallpaperItem> result;
    std::error_code ec;
    for (auto const& folder : request.importedFolders) {
        if (auto item = inspectWallpaperFolder(folder)) result.push_back(std::move(*item));
    }

    auto searchDirs = collectScanDirectories(request);
    for (auto const& searchDir : searchDirs) {
        if (!std::filesystem::exists(searchDir, ec) || !std::filesystem::is_directory(searchDir, ec)) {
            continue;
        }

        for (auto const& entry : std::filesystem::directory_iterator(searchDir, ec)) {
            if (entry.is_directory(ec)) {
                auto itemOpt = inspectWallpaperFolder(entry.path());
                if (itemOpt.has_value()) {
                    // check duplicates by id or path
                    bool duplicate = false;
                    for (auto const& existing : result) {
                        if (existing.id == itemOpt->id || existing.rootPath == itemOpt->rootPath) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        result.push_back(std::move(*itemOpt));
                    }
                }
            }
        }
    }

    // compatibility is decided here, off the main thread: scene checks read
    // the package directory and must not run on a list click.
    for (auto& item : result) {
        item.playable = resolveWallpaperPlayback(item).has_value();
        if (item.playable && isPerspectiveScene(item)) {
            item.scene3D = true;
            item.playable = false;
        }
    }

    {
        std::lock_guard lock(m_scanCacheMutex);
        m_scanCache = result;
        m_scanCacheTime = std::chrono::steady_clock::now();
        m_scanCacheValid = true;
    }

    return result;
}

std::vector<WallpaperItem> WallpaperEngineManager::filterByType(
    std::vector<WallpaperItem> const& list,
    WallpaperType filter
) {
    if (filter == WallpaperType::All) return list;
    std::vector<WallpaperItem> filtered;
    for (auto const& item : list) {
        if (item.type == filter || (filter == WallpaperType::Gif && item.type == WallpaperType::Image)) {
            filtered.push_back(item);
        }
    }
    return filtered;
}

std::string WallpaperEngineManager::getWorkshopWebUrl(WallpaperItem const& item) const {
    if (item.workshopId == 0) return {};
    return "https://steamcommunity.com/sharedfiles/filedetails/?id=" + std::to_string(item.workshopId);
}

std::string WallpaperEngineManager::getWorkshopSteamUri(WallpaperItem const& item) const {
    if (item.workshopId == 0) return {};
    return "steam://url/CommunityFilePage/" + std::to_string(item.workshopId);
}

void WallpaperEngineManager::openWorkshopPage(WallpaperItem const& item) const {
    auto webUrl = getWorkshopWebUrl(item);
    if (!webUrl.empty()) {
        geode::utils::web::openLinkInBrowser(webUrl);
    }
}

bool WallpaperEngineManager::applyWallpaper(std::string const& layerKey, WallpaperItem const& item) {
    auto const playback = resolveWallpaperPlayback(item);
    if (!playback) return false;
    if (item.type == WallpaperType::Web && !WebWallpaperRenderer::isAvailable()) return false;

    auto& bgMgr = LayerBackgroundManager::get();
    auto cfg = bgMgr.getConfig(layerKey);

    cfg.weTitle = item.title;
    cfg.weAuthor = item.author;
    cfg.weWorkshopId = item.workshopId;

    cfg.type = playback->backgroundType();
    cfg.customPath = paimon::paths::toUtf8(playback->path);

    bgMgr.saveConfig(layerKey, cfg);
    return true;
}

char const* WallpaperEngineManager::typeToString(WallpaperType type) {
    switch (type) {
        case WallpaperType::Video: return "VIDEO";
        case WallpaperType::Web: return "WEB";
        case WallpaperType::Scene: return "SCENE";
        case WallpaperType::Gif: return "GIF";
        case WallpaperType::Image: return "IMAGEN";
        case WallpaperType::Procedural: return "SHADER";
        default: return "OTROS";
    }
}

WallpaperType WallpaperEngineManager::stringToType(std::string const& str) {
    std::string s;
    for (char c : str) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (s == "video") return WallpaperType::Video;
    if (s == "web") return WallpaperType::Web;
    if (s == "scene") return WallpaperType::Scene;
    if (s == "gif") return WallpaperType::Gif;
    if (s == "image") return WallpaperType::Image;
    if (s == "procedural" || s == "shader") return WallpaperType::Procedural;
    return WallpaperType::Unsupported;
}

} // namespace paimon::wallpaper
