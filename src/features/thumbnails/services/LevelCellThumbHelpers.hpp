#pragma once

// Helpers y enums standalone extraidos de hooks/LevelCell.cpp.
// Logica pura de presentacion de thumbnails de LevelCell, sin dependencia
// del estado del hook ($modify Fields / this). Vive en la feature de
// thumbnails para que el hook quede delgado.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "../../../managers/ThumbnailAPI.hpp"

namespace cocos2d { class CCSprite; class CCNode; class CCClippingNode; }

namespace paimon::levelcell {

enum class PaimonAnimType : uint8_t {
    None, ZoomSlide, Zoom, Slide, Bounce, Rotate, RotateContent, Shake, Pulse, Swing
};

enum class PaimonAnimEffect : uint8_t {
    None, Brightness, Darken, Sepia, Sharpen, EdgeDetection, Vignette, Pixelate,
    Posterize, Chromatic, Scanlines, Solarize, Rainbow, Red, Blue, Gold, Fade,
    Grayscale, Invert, Blur, Glitch
};

enum class PaimonBgType : uint8_t { Gradient, Thumbnail };

enum class PaimonGalleryTransition : uint8_t {
    Crossfade, SlideLeft, SlideRight, SlideUp, SlideDown,
    ZoomIn, ZoomOut, FlipHorizontal, FlipVertical,
    RotateCW, RotateCCW, Cube, Dissolve, Swipe, Bounce,
    ElasticSlide, DirectionalElastic, Spiral, Wave, Pop,
    Random
};

inline constexpr int LEVELCELL_GALLERY_LOOKAHEAD = 2;
inline constexpr int LEVELCELL_GALLERY_SEARCH_WINDOW = 3;
inline constexpr size_t LEVELCELL_GALLERY_MAX_PENDING = 3;
inline constexpr float LEVELCELL_GALLERY_RETRY_DELAY = 8.0f;
inline constexpr int LEVELCELL_GALLERY_MAX_MISSES = 2;
// Espaciado al re-entrar en una lista (evita picos de FPS con N celdas visibles).
inline constexpr float LEVELCELL_GALLERY_REENTER_STAGGER = 0.05f;
inline constexpr int LEVELCELL_GALLERY_REENTER_STAGGER_SLOTS = 12;
// 0.0f => el scheduler ejecuta el selector CADA frame (con el dt real del frame).
// Antes estaba fijado a 1/30 (30 FPS) lo que hacia que las animaciones de hover/zoom
// y el gradiente se vieran "saltadas" / como a pocos FPS aunque el juego corriera a 60+.
// Toda la matematica de animacion ya usa dt (m_animTime += dt, m_centerLerp con dt*speed),
// asi que la VELOCIDAD se mantiene identica; solo se vuelve fluida al framerate nativo.
inline constexpr float LEVELCELL_VISUAL_TICK_INTERVAL = 0.0f;
inline constexpr float LEVELCELL_MAINTENANCE_INTERVAL = 0.4f;

float safeCoverScale(float targetWidth, float targetHeight, float contentWidth, float contentHeight, float fallback = 1.0f);
float getLevelCellThumbWidthFactor();
float calculateLevelCellThumbCoverScale(cocos2d::CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float fallback = 1.0f);
void calculateLevelCellThumbScale(cocos2d::CCSprite* sprite, float bgWidth, float bgHeight, float widthFactor, float& outScaleX, float& outScaleY);
cocos2d::CCClippingNode* createThumbnailClippingNode(cocos2d::CCNode* bg, cocos2d::CCSprite* sprite, float& outCoverScale);
std::vector<ThumbnailAPI::ThumbnailInfo> normalizeLevelCellGalleryThumbnails(int32_t levelID, std::vector<ThumbnailAPI::ThumbnailInfo> thumbnails);
std::string makeLevelCellBlurCacheKey(int32_t levelID, int galleryIndex, float blurIntensity, bool isBackground);
PaimonAnimType parseAnimType(std::string const& s);
PaimonAnimEffect parseAnimEffect(std::string const& s);
PaimonBgType parseBgType(std::string const& s);
PaimonGalleryTransition parseGalleryTransition(std::string const& s);
PaimonGalleryTransition resolveRandomTransition();

// true si la celda deberia usar prioridad alta de descarga (visible o sin layout aun).
bool isLevelCellLikelyOnScreen(cocos2d::CCNode* cell);

} // namespace paimon::levelcell
