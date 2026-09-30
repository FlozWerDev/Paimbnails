#include "GLSLLoader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/shaders/CCShaderCache.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <system_error>

using namespace cocos2d;

namespace paimon::shaders {

namespace {

// ram cache of file contents, populated lazily. shared_mutex: reads dominate.
struct ShaderSourceCache {
    std::shared_mutex mutex;
    std::unordered_map<std::string, std::string> contents;
};

ShaderSourceCache& sourceCache() {
    // heap-allocated: no destructors at exit, dodges static-order crashes across dlls.
    static auto* cache = new ShaderSourceCache();
    return *cache;
}

std::filesystem::path shadersDir() {
    // dev layout: src/../resources/shaders/<name>.glsl
    return geode::Mod::get()->getResourcesDir() / "shaders";
}

// installed .geode flattens resources/ (no subfolders); try dev layout then flat.
std::filesystem::path shadersDirFlat() {
    return geode::Mod::get()->getResourcesDir();
}

std::string readFileRaw(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return {};

    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    std::ostringstream buf;
    buf << f.rdbuf();
    if (!f && !f.eof()) return {};
    return buf.str();
}

} // namespace

std::string readShaderFile(std::string_view relName) {
    std::string key(relName);

    {
        std::shared_lock<std::shared_mutex> lock(sourceCache().mutex);
        auto it = sourceCache().contents.find(key);
        if (it != sourceCache().contents.end()) {
            return it->second;
        }
    }

    auto contents = readFileRaw(shadersDir() / key);
    if (contents.empty()) {
        contents = readFileRaw(shadersDirFlat() / key);
    }

    {
        std::unique_lock<std::shared_mutex> lock(sourceCache().mutex);
        // another thread may have inserted while we read.
        auto [it, inserted] = sourceCache().contents.emplace(std::move(key), std::move(contents));
        return it->second;
    }
}

namespace {

// mod keys inside ccshadercache. main thread only; heap-allocated like sourcecache().
std::unordered_set<std::string>& trackedShaderKeys() {
    static auto* keys = new std::unordered_set<std::string>();
    return *keys;
}

} // namespace

void trackShaderKey(std::string const& key) {
    trackedShaderKeys().insert(key);
}

void purgeTrackedShaders() {
    auto* shaderCache = CCShaderCache::sharedShaderCache();
    if (!shaderCache || !shaderCache->m_pPrograms) {
        trackedShaderKeys().clear();
        return;
    }
    size_t removed = 0;
    for (auto const& key : trackedShaderKeys()) {
        if (shaderCache->m_pPrograms->objectForKey(key)) {
            shaderCache->m_pPrograms->removeObjectForKey(key);
            ++removed;
        }
    }
    trackedShaderKeys().clear();
    geode::log::info("[GLSLLoader] purgeTrackedShaders: {} programs removed from CCShaderCache", removed);
}

CCGLProgram* loadShader(
    std::string_view cacheKey,
    std::string_view vertexFile,
    std::string_view fragmentFile,
    char const* vertexFallback,
    char const* fragmentFallback
) {
    if (cacheKey.empty()) {
        geode::log::error("[GLSLLoader] loadShader called with empty cacheKey");
        return nullptr;
    }

    auto* shaderCache = CCShaderCache::sharedShaderCache();
    std::string keyStr(cacheKey);
    if (auto* program = shaderCache->programForKey(keyStr.c_str())) {
        return program;
    }

    std::string vertexFromDisk;
    if (!vertexFile.empty()) {
        vertexFromDisk = readShaderFile(vertexFile);
    }
    char const* vertexSrc = vertexFromDisk.empty() ? vertexFallback : vertexFromDisk.c_str();

    std::string fragmentFromDisk;
    if (!fragmentFile.empty()) {
        fragmentFromDisk = readShaderFile(fragmentFile);
    }
    char const* fragmentSrc = fragmentFromDisk.empty() ? fragmentFallback : fragmentFromDisk.c_str();

    if (!vertexSrc || !*vertexSrc || !fragmentSrc || !*fragmentSrc) {
        geode::log::error(
            "[GLSLLoader] No source for shader '{}' (vertexFile='{}', fragmentFile='{}')",
            keyStr, vertexFile, fragmentFile);
        return nullptr;
    }

    auto* program = new CCGLProgram();
    if (!program->initWithVertexShaderByteArray(vertexSrc, fragmentSrc)) {
        geode::log::error("[GLSLLoader] initWithVertexShaderByteArray failed for '{}'", keyStr);
        program->release();
        return nullptr;
    }

    // standard cocos2d attribute names; all mod shaders use these.
    program->addAttribute("a_position", kCCVertexAttrib_Position);
    program->addAttribute("a_color", kCCVertexAttrib_Color);
    program->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);

    if (!program->link()) {
        geode::log::error("[GLSLLoader] link failed for '{}'", keyStr);
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, keyStr.c_str());
    program->release();
    trackShaderKey(keyStr);
    return shaderCache->programForKey(keyStr.c_str());
}

// per-shader cache keys ("-v3") avoid clashes with old keys on hot updates.
// all pass nullptr fallback: fail fast when the .glsl is missing.

CCGLProgram* getBlurHorizontalShader() {
    return loadShader("paimon-blur-h-v3", "cell_vertex.glsl", "blur_h.glsl", nullptr, nullptr);
}

CCGLProgram* getBlurVerticalShader() {
    return loadShader("paimon-blur-v-v3", "cell_vertex.glsl", "blur_v.glsl", nullptr, nullptr);
}

CCGLProgram* getKawaseDownShader() {
    return loadShader("paimon-kawase-down-v3", "cell_vertex.glsl", "kawase_down.glsl", nullptr, nullptr);
}

CCGLProgram* getKawaseUpShader() {
    return loadShader("paimon-kawase-up-v3", "cell_vertex.glsl", "kawase_up.glsl", nullptr, nullptr);
}

CCGLProgram* getKawaseRealtimeShader() {
    return loadShader("paimon-kawase-rt-v3", "cell_vertex.glsl", "kawase_realtime.glsl", nullptr, nullptr);
}

CCGLProgram* getBlurCellShader() {
    return loadShader("paimon-blur-cell-v3", "cell_vertex.glsl", "kawase_cell.glsl", nullptr, nullptr);
}

CCGLProgram* getBlurSinglePassShader() {
    return loadShader("paimon-blur-single-v3", "cell_vertex.glsl", "blur_single.glsl", nullptr, nullptr);
}

CCGLProgram* getBlurFastShader() {
    return loadShader("paimon-blur-fast-v3", "cell_vertex.glsl", "blur_fast.glsl", nullptr, nullptr);
}

CCGLProgram* getYUVShader() {
    return loadShader("paimon-yuv-v1", "yuv_vertex.glsl", "yuv_fragment.glsl", nullptr, nullptr);
}

CCGLProgram* getYUVBlitShader() {
    return loadShader("paimon-yuv-blit-v1", "yuv_vertex.glsl", "yuv_to_rgba_blit.glsl", nullptr, nullptr);
}

CCGLProgram* getDominantColorsDownsampleShader() {
    return loadShader("paimon-dc-downsample-v1", "cell_vertex.glsl", "dominant_colors_downsample.glsl", nullptr, nullptr);
}

CCGLProgram* getGifDownscaleShader() {
    return loadShader("paimon-gif-downscale-v1", "cell_vertex.glsl", "gif_downscale.glsl", nullptr, nullptr);
}

CCGLProgram* getGifBlurShader() {
    return loadShader("paimon-gif-blur-v1", "cell_vertex.glsl", "gif_blur.glsl", nullptr, nullptr);
}

CCGLProgram* getTintPreviewShader() {
    return loadShader(
        "paimon-tint-preview-v1",
        "cell_vertex.glsl",
        "tint_preview.glsl",
        nullptr,
        nullptr
    );
}

} // namespace paimon::shaders
