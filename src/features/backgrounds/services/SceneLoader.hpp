#pragma once

#include "SceneData.hpp"
#include "ScenePackage.hpp"
#include "SceneTexture.hpp"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace paimon::wallpaper {

// what an uploaded texture can still serve without decoding again.
struct SceneCoverage {
    int cellW = 0, cellH = 0;
    bool full = false;
    bool satisfies(int targetW, int targetH) const {
        if (full) return true;
        if (targetW <= 0 || targetH <= 0) return false;
        return cellW + 1 >= targetW && cellH + 1 >= targetH;
    }
};

struct SceneDecodeCaps {
    bool keepCompressed = false;
    int maxDimension = 4096;
    float viewportW = 0.f, viewportH = 0.f;
    float pixelsPerPoint = 1.f;
    float overscan = 1.f;
    std::string fitMode = "cover";
    // where embedded MP4 textures are extracted; empty skips video layers.
    std::filesystem::path videoCacheDir;
};

// parses scene.json and every referenced model/material/particle file.
// worker-thread safe; no cocos.
std::shared_ptr<SceneData> extractSceneData(ScenePackage const& package);

// framebuffer pixels per scene unit for the given viewport.
float pixelsPerSceneUnit(SceneData const& data, SceneDecodeCaps const& caps);

// pixel target a texture needs on screen (0 = keep full resolution).
std::pair<int, int> sceneTextureTarget(SceneData const& data, std::string const& path, float pixelsPerUnit);

SceneCoverage coverageOf(SceneTexture const& texture);

// decodes (in parallel) every texture `cached` cannot serve, at the smallest
// mip level that still covers its on-screen size. Returns false if
// `cancelled` stopped the work.
bool decodeSceneTextures(ScenePackage const& package, SceneData const& data,
    std::unordered_map<std::string, SceneCoverage> const& cached, SceneDecodeCaps const& caps,
    std::function<bool()> const& cancelled, std::vector<std::pair<std::string, SceneTexture>>& out);

} // namespace paimon::wallpaper
