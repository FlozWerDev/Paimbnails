#pragma once
// load GLSL programs from resources/shaders/, with optional inline fallbacks.

#include <Geode/cocos/shaders/CCGLProgram.h>
#include <string>
#include <string_view>

namespace paimon::shaders {

// cached program; nullptr when sources are missing and no fallback given.
cocos2d::CCGLProgram* loadShader(
    std::string_view cacheKey,
    std::string_view vertexFile,
    std::string_view fragmentFile,
    char const* vertexFallback,
    char const* fragmentFallback
);

// read and cache resources/shaders/<relName>; empty means missing/unreadable.
std::string readShaderFile(std::string_view relName);

// track a mod-owned CCShaderCache key for later purging. main thread only.
void trackShaderKey(std::string const& key);

// purge mod programs before GL context recreation; rebuilt lazily.
void purgeTrackedShaders();

// typed helpers wrap cache keys and shader files.

cocos2d::CCGLProgram* getBlurHorizontalShader();
cocos2d::CCGLProgram* getBlurVerticalShader();
cocos2d::CCGLProgram* getKawaseDownShader();
cocos2d::CCGLProgram* getKawaseUpShader();
cocos2d::CCGLProgram* getKawaseRealtimeShader();

// single-pass 9x9 cell blur.
cocos2d::CCGLProgram* getBlurCellShader();

// cheaper single-pass dual-kawase blur for animated sprites.
cocos2d::CCGLProgram* getBlurSinglePassShader();

// fixed 3.5px fallback blur for ProfileThumbs.
cocos2d::CCGLProgram* getBlurFastShader();

// PaiblurNode embeds its dynamic shader and bypasses this loader.

// VideoPlayer three-plane YUV->RGB shader.
cocos2d::CCGLProgram* getYUVShader();

// blit YUV planes into an RGBA FBO for VideoPlayer.
cocos2d::CCGLProgram* getYUVBlitShader();

// pre-reduce sRGB->LAB into a small FBO for CPU-side k-means.
cocos2d::CCGLProgram* getDominantColorsDownsampleShader();

// halve a frame with an alpha-weighted box filter for the GIF importer.
cocos2d::CCGLProgram* getGifDownscaleShader();
cocos2d::CCGLProgram* getGifBlurShader();

// live PackGen tint for the texture-studio preview. null when
// tint_preview.glsl is missing; the editor falls back to CPU render.
cocos2d::CCGLProgram* getTintPreviewShader();

}
