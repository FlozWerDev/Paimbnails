#pragma once

#include <cocos2d.h>
#include <cstddef>
#include <string>

namespace paimon::image {

// CCTextureCache never drops what it loads, so every distinct cover or thumbnail
// the player looks at stays in VRAM for the rest of the session. These helpers go
// through the cache as usual but keep only the last `budget` paths registered in
// it: evicting just removes the cache's own reference, so sprites still using the
// texture keep working and the memory comes back when they go away.
//
// Main thread only, like the rest of CCTextureCache.

// Enough for a scrolled list of covers plus the ones on screen.
inline constexpr std::size_t kDiskTextureBudget = 24;

cocos2d::CCTexture2D* loadBudgeted(
    std::string const& absolutePath, std::size_t budget = kDiskTextureBudget);

// Drops a path from CCTextureCache and from the budget, for when the caller knows
// the image changed on disk or will not be shown again.
void dropBudgeted(std::string const& absolutePath);

void clearBudgeted();

} // namespace paimon::image
