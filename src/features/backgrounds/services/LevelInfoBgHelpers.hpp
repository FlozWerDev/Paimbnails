#pragma once

// Constantes de z-order y helper de cache de blur para el fondo de
// LevelInfoLayer. Extraido de hooks/LevelInfoLayer.cpp para sacar
// logica de presentacion del hook.

#include <cocos2d.h>
#include <string>

namespace paimon::levelinfo {

// Z-order constants for LevelInfoLayer background layering
inline constexpr int kBackgroundZOrder = -4;
inline constexpr int kExtraDarknessZOrder = -3; // oscuridad extra separada del bg
inline constexpr int kEffectsZOrder   = -2;
inline constexpr int kOverlayZOrder   = -1;

std::string makeLevelInfoBlurCacheKey(int levelID, int thumbnailIndex, std::string const& bgStyle, int intensity, cocos2d::CCSize const& targetSize);

} // namespace paimon::levelinfo
