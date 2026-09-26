#pragma once

#include <cocos2d.h>
#include <cstddef>
#include <string>

namespace paimon::image {

// CCTextureCache never releases; only the last `budget` entries stay.

// for lists with many covers.
inline constexpr std::size_t kDiskTextureBudget = 24;

cocos2d::CCTexture2D* loadBudgeted(
    std::string const& absolutePath, std::size_t budget = kDiskTextureBudget);

void dropBudgeted(std::string const& absolutePath);

void clearBudgeted();

} // namespace paimon::image
