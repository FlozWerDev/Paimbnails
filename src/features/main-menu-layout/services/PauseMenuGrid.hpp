#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace paimon::menu_layout {

struct PauseMenuSlot {
    float x;
    float y;
    float iconSize;
    float captionWidth;
    bool showCaption;
};

// screen coordinates keep both sidebars clear of the title, audio and StartPos.
inline std::vector<PauseMenuSlot> pauseMenuSlots(float width, float height,
    std::size_t count, bool rightSide) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 320.f || height < 180.f || !count) {
        return {};
    }

    auto const rows = (count + 1) / 2;
    float const cellWidth = std::clamp(width * .078f, 36.f, 48.f);
    float const stepY = std::min(51.f, (height - 100.f) / static_cast<float>(rows));
    bool const showCaption = stepY >= 34.f;
    float const iconSize = std::max(1.f, std::min(32.f, stepY - (showCaption ? 17.f : 4.f)));
    float const centerX = rightSide ? width - 16.f - cellWidth : 16.f + cellWidth;
    float const topY = height * .5f + 8.f + (static_cast<float>(rows) - 1.f) * stepY * .5f;

    std::vector<PauseMenuSlot> slots;
    slots.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        bool const singleLast = count % 2 && index == count - 1;
        float const column = singleLast ? 0.f : (index % 2 ? .5f : -.5f);
        slots.push_back({centerX + column * cellWidth,
            topY - static_cast<float>(index / 2) * stepY, iconSize, cellWidth - 4.f, showCaption});
    }
    return slots;
}

} // namespace paimon::menu_layout
