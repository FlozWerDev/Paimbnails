#pragma once
// GLSLLoader — loads GLSL programs from disk files with inline-literal fallback.
// Shaders live in dedicated .glsl files; if a file is missing, loadShader
// fail-fasts (log::error + nullptr) unless a fallback is provided.
//
// Thread-safety: GPU load functions MUST run on the main (GL) thread. File
// reads (readShaderFile) are reentrant and cache contents to avoid re-I/O.

#include <Geode/cocos/shaders/CCGLProgram.h>
#include <string>
#include <string_view>

namespace paimon::shaders {

/// Load (or return from CCShaderCache) a CCGLProgram compiled from the given
/// files. Fail-fast: if the files can't be read and the fallback is nullptr,
/// logs an error and returns nullptr (callers MUST check — nullptr means a
/// broken install). Fallbacks are emergency-only, not the main path.
///
/// @param cacheKey         stable CCShaderCache key.
/// @param vertexFile       name relative to resources/shaders/ (empty → use fallback).
/// @param fragmentFile     same for the fragment shader.
/// @param vertexFallback   inline source, or nullptr for fail-fast.
/// @param fragmentFallback same.
/// @return the compiled/cached CCGLProgram, or nullptr on failure.
cocos2d::CCGLProgram* loadShader(
    std::string_view cacheKey,
    std::string_view vertexFile,
    std::string_view fragmentFile,
    char const* vertexFallback,
    char const* fragmentFallback
);

/// Read resources/shaders/<relName> as UTF-8/ASCII, caching the result in RAM.
/// Returns an empty string if the file is missing or unreadable. Thread-safe.
std::string readShaderFile(std::string_view relName);

/// Preload the blur shaders on the GL thread so the first use doesn't pay the
/// compile stutter. Idempotent. MUST run on the main (GL) thread.
void preloadBlurShaders();

/// Clear the RAM cache of read GLSL sources. Useful for hot-reload in debug.
/// Doesn't touch CCShaderCache — that's the caller's job.
void clearShaderFileCache();

// Typed helpers for the mod's blur shaders, each wrapping its cache key and
// .glsl files. Prefer these over getOrCreateShader.

cocos2d::CCGLProgram* getBlurHorizontalShader();
cocos2d::CCGLProgram* getBlurVerticalShader();
cocos2d::CCGLProgram* getKawaseDownShader();
cocos2d::CCGLProgram* getKawaseUpShader();
cocos2d::CCGLProgram* getKawaseRealtimeShader();

/// High-quality single-pass "cell" blur (9x9 taps). Used for LevelCell /
/// GJScoreCell / BadgeHooks / CommunityHubLayer, on static and animated sprites.
cocos2d::CCGLProgram* getBlurCellShader();

/// Single-pass dual-Kawase 12-tap — cheaper than getBlurCellShader, for
/// animated GIFs in LevelInfoLayer / InfoLayer.
cocos2d::CCGLProgram* getBlurSinglePassShader();

/// Fixed-intensity (3.5 px) fast blur. ProfileThumbs fallback when BlurSystem's
/// realtime path is unavailable.
cocos2d::CCGLProgram* getBlurFastShader();

// Note: the dynamic Paiblur path (PaiblurNode) embeds its EclipseMenu-style
// blur shaders directly in PaiblurNode.cpp (raw GL program, no cocos pipeline),
// so it no longer loads anything through this loader.

/// YUV→RGB GPU shader for VideoPlayer. Uses 3 luminance textures (Y, Cb, Cr)
/// and converts to RGB on the GPU, eliminating CPU-side SIMD conversion.
cocos2d::CCGLProgram* getYUVShader();

/// YUV→RGBA blit shader — renders YUV planes to an RGBA FBO.
/// Used by VideoPlayer::resolveToRGBA() to produce an RGBA CCTexture2D
/// without CPU-side SIMD conversion. Eliminates the forceRGBA path.
cocos2d::CCGLProgram* getYUVBlitShader();

/// DominantColors GPU pre-reduction shader. Converts sRGB→LAB on the GPU
/// and outputs encoded LAB values to a tiny FBO for CPU-side K-means.
cocos2d::CCGLProgram* getDominantColorsDownsampleShader();

} // namespace paimon::shaders
