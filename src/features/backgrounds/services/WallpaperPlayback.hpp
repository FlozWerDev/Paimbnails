#pragma once

#include "WallpaperEngineManager.hpp"
#include "ScenePackage.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace paimon::wallpaper {

struct WallpaperPlayback {
    std::filesystem::path path;
    WallpaperType type;

    char const* backgroundType() const {
        if (type == WallpaperType::Web) return "wallpaper";
        if (type == WallpaperType::Scene) return "scene";
        return type == WallpaperType::Video ? "video" : "custom";
    }
};

inline std::optional<WallpaperPlayback> wallpaperMediaFile(std::filesystem::path const& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return std::nullopt;

    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (ext == ".mp4" || ext == ".webm" || ext == ".mov" || ext == ".m4v" ||
        ext == ".mpg" || ext == ".mpeg" || ext == ".avi" || ext == ".wmv" || ext == ".mkv") {
        return WallpaperPlayback{path, WallpaperType::Video};
    }
    if (ext == ".gif") return WallpaperPlayback{path, WallpaperType::Gif};
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
        return WallpaperPlayback{path, WallpaperType::Image};
    }
    return std::nullopt;
}

inline std::optional<WallpaperPlayback> resolveWallpaperPlayback(WallpaperItem const& item) {
    if (item.type == WallpaperType::Video || item.type == WallpaperType::Gif ||
        item.type == WallpaperType::Image) {
        auto media = wallpaperMediaFile(item.entryFile);
        return media && media->type == item.type ? media : std::nullopt;
    }
    if (item.type != WallpaperType::Web && item.type != WallpaperType::Scene) {
        return std::nullopt;
    }

    std::error_code ec;
    auto const project = item.rootPath / "project.json";
    if (item.rootPath.empty() || !std::filesystem::is_regular_file(project, ec)) return std::nullopt;
    if (item.type == WallpaperType::Scene && !ScenePackage::open(item.rootPath)) return std::nullopt;
    return WallpaperPlayback{project, item.type};
}

} // namespace paimon::wallpaper
