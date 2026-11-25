#pragma once

#include <cocos2d.h>
#include <cstddef>
#include <functional>
#include <string>

namespace paimon::image {

// cctexturecache never releases; only the last `budget` entries stay.

// for lists with many covers.
inline constexpr std::size_t kDiskTextureBudget = 24;

cocos2d::CCTexture2D* loadBudgeted(
    std::string const& absolutePath, std::size_t budget = kDiskTextureBudget);

// concurrent requests for one path share a single background decode/upload.
// callbacks run on the main thread; callers guard their UI's lifetime.
void loadBudgetedAsync(std::string const& absolutePath,
    std::function<void(cocos2d::CCTexture2D*)> callback);

// register an asynchronously cached texture in the same bounded LRU. Main thread only.
void touchBudgeted(std::string const& absolutePath, std::size_t budget = kDiskTextureBudget);

void dropBudgeted(std::string const& absolutePath);

void clearBudgeted();

} // namespace paimon::image
