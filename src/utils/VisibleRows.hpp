#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace paimon::ui {

struct VisibleRowRange {
    std::size_t begin = 0;
    std::size_t end = 0; // exclusive
    bool operator==(VisibleRowRange const&) const = default;
};

// rows run from the content's top down; Cocos scroll content moves in +y.
inline VisibleRowRange visibleRows(std::size_t count, float pitch, float contentHeight,
    float viewHeight, float contentY, std::size_t overscan = 1) {
    if (!count || !std::isfinite(pitch) || pitch <= 0.f ||
        !std::isfinite(contentHeight) || !std::isfinite(viewHeight) || viewHeight <= 0.f ||
        !std::isfinite(contentY)) return {};
    double const offset = std::clamp(static_cast<double>(contentHeight) - viewHeight + contentY,
        0.0, std::max(0.0, static_cast<double>(contentHeight) - viewHeight));
    auto const first = static_cast<std::size_t>(std::min<double>(count, std::floor(offset / pitch)));
    auto const last = static_cast<std::size_t>(std::min<double>(count, std::ceil((offset + viewHeight) / pitch)));
    return {first - std::min(first, overscan), last + std::min(count - last, overscan)};
}

}
