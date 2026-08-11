#pragma once
// Load GLSL programs from resources/shaders/ with optional inline fallbacks.
// GPU operations run on the GL thread; file reads are cached and reentrant.

#include <Geode/cocos/shaders/CCGLProgram.h>
#include <string>
#include <string_view>

namespace paimon::shaders {

/// Load or retrieve a cached program; returns nullptr when sources are missing
/// and no fallback is provided.
cocos2d::CCGLProgram* loadShader(
    std::string_view cacheKey,
    std::string_view vertexFile,
    std::string_view fragmentFile,
    char const* vertexFallback,
    char const* fragmentFallback
);

/// Read and cache resources/shaders/<relName>; empty means missing/unreadable.
std::string readShaderFile(std::string_view relName);

/// Preload blur shaders on the GL thread; idempotent.
void preloadBlurShaders();

/// Clear the source cache without touching CCShaderCache.
void clearShaderFileCache();

/// Track a mod-owned CCShaderCache key for later purging. Main thread only.
void trackShaderKey(std::string const& key);

/// Purge mod programs before GL context recreation; they are rebuilt lazily.
void purgeTrackedShaders();

// Typed helpers wrap cache keys and shader files.

cocos2d::CCGLProgram* getBlurHorizontalShader();
cocos2d::CCGLProgram* getBlurVerticalShader();
cocos2d::CCGLProgram* getKawaseDownShader();
cocos2d::CCGLProgram* getKawaseUpShader();
cocos2d::CCGLProgram* getKawaseRealtimeShader();

/// High-quality single-pass 9×9 cell blur.
cocos2d::CCGLProgram* getBlurCellShader();

/// Cheaper single-pass dual-Kawase blur for animated sprites.
cocos2d::CCGLProgram* getBlurSinglePassShader();

/// Fixed 3.5 px fallback blur for ProfileThumbs.
cocos2d::CCGLProgram* getBlurFastShader();

// PaiblurNode embeds its dynamic shader and bypasses this loader.

/// VideoPlayer's three-plane YUV→RGB shader.
cocos2d::CCGLProgram* getYUVShader();

/// Blit YUV planes into an RGBA FBO for VideoPlayer.
cocos2d::CCGLProgram* getYUVBlitShader();

/// Pre-reduce sRGB→LAB into a small FBO for CPU-side K-means.
cocos2d::CCGLProgram* getDominantColorsDownsampleShader();

}
