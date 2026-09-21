#pragma once

#include <cocos2d.h>
#include <cstddef>
#include <string>

namespace paimon::image {

// CCTextureCache no suelta nada; aqui solo se quedan las ultimas `budget`.

// Para listas con muchas portadas.
inline constexpr std::size_t kDiskTextureBudget = 24;

cocos2d::CCTexture2D* loadBudgeted(
    std::string const& absolutePath, std::size_t budget = kDiskTextureBudget);

void dropBudgeted(std::string const& absolutePath);

void clearBudgeted();

} // namespace paimon::image
