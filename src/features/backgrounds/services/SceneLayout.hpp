#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace paimon::wallpaper {

// wallpaper Engine scene space: root objects use 0-based coordinates measured
// from the bottom-left corner of the orthogonal projection (y up), and an
// object's `origin` is its centre. Children are expressed in their parent's
// local space, so their origins are centred on the parent. `angles` are
// radians, counter-clockwise. Everything here is cocos-free so it can be
// verified on its own.

struct SceneAnchor {
    float x = 0.5f;
    float y = 0.5f;
};

// `alignment` carries "left"/"center"/"right", optionally combined with
// "top"/"bottom" (e.g. "topleft"). Unknown or empty values stay centred.
inline SceneAnchor anchorForAlignment(std::string_view horizontal, std::string_view vertical) {
    auto contains = [](std::string_view text, std::string_view needle) {
        if (text.empty() || needle.empty() || needle.size() > text.size()) return false;
        for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
            size_t j = 0;
            for (; j < needle.size(); ++j) {
                auto const lhs = std::tolower(static_cast<unsigned char>(text[i + j]));
                auto const rhs = static_cast<char>(needle[j]);
                if (lhs != rhs) break;
            }
            if (j == needle.size()) return true;
        }
        return false;
    };

    SceneAnchor anchor;
    // wallpaper Engine may pack both axes into the single `alignment` string
    // ("topleft"), so each axis falls back to the other field.
    std::string_view const horizontalText = horizontal.empty() ? vertical : horizontal;
    std::string_view const verticalText = vertical.empty() ? horizontal : vertical;
    if (contains(horizontalText, "left")) anchor.x = 0.f;
    else if (contains(horizontalText, "right")) anchor.x = 1.f;
    if (contains(verticalText, "top")) anchor.y = 1.f;
    else if (contains(verticalText, "bottom")) anchor.y = 0.f;
    return anchor;
}

// `orthogonalprojection.auto` sizes the projection to the farthest object edge
// measured from the 0-based origin.
inline float sceneExtentFromEdges(float maxEdge) {
    return maxEdge > 0.f && std::isfinite(maxEdge) ? maxEdge : 0.f;
}

// displacement of one layer for a normalised pointer axis (-1..1). Wallpaper
// engine scales the movement by the authored parallaxDepth, so a depth of zero
// is a static background layer and the foreground travels furthest.
inline float parallaxOffset(float depth, float axis, float strength, float range = 25.f) {
    return depth * axis * strength * range;
}

// 2D affine: x' = a*x + c*y + tx, y' = b*x + d*y + ty (same layout as cocos).
struct SceneAffine {
    float a = 1.f, b = 0.f, c = 0.f, d = 1.f, tx = 0.f, ty = 0.f;
};

inline SceneAffine sceneMultiply(SceneAffine const& parent, SceneAffine const& local) {
    return {
        parent.a * local.a + parent.c * local.b,
        parent.b * local.a + parent.d * local.b,
        parent.a * local.c + parent.c * local.d,
        parent.b * local.c + parent.d * local.d,
        parent.a * local.tx + parent.c * local.ty + parent.tx,
        parent.b * local.tx + parent.d * local.ty + parent.ty,
    };
}

// translate * rotate(counter-clockwise radians) * scale
inline SceneAffine sceneLocalTransform(float x, float y, float radians, float scaleX, float scaleY) {
    float const cs = std::cos(radians), sn = std::sin(radians);
    return {cs * scaleX, sn * scaleX, -sn * scaleY, cs * scaleY, x, y};
}

inline SceneAffine sceneScaled(SceneAffine m, float scaleX, float scaleY) {
    m.a *= scaleX; m.b *= scaleX;
    m.c *= scaleY; m.d *= scaleY;
    return m;
}

inline void sceneApply(SceneAffine const& m, float x, float y, float& outX, float& outY) {
    outX = m.a * x + m.c * y + m.tx;
    outY = m.b * x + m.d * y + m.ty;
}

// cCNode properties reproducing an arbitrary affine: cocos rotates each axis
// independently (rotationX / rotationY, clockwise degrees), which also covers
// mirrored and sheared parents.
struct SceneNodeTransform {
    float x = 0.f, y = 0.f;
    float scaleX = 1.f, scaleY = 1.f;
    float rotationX = 0.f, rotationY = 0.f;
};

inline SceneNodeTransform decomposeSceneAffine(SceneAffine const& m) {
    constexpr float kDegrees = 57.29577951308232f;
    SceneNodeTransform t;
    t.x = m.tx;
    t.y = m.ty;
    t.scaleX = std::hypot(m.a, m.b);
    t.scaleY = std::hypot(m.c, m.d);
    t.rotationY = t.scaleX > 0.f ? -std::atan2(m.b, m.a) * kDegrees : 0.f;
    t.rotationX = t.scaleY > 0.f ? -std::atan2(-m.c, m.d) * kDegrees : 0.f;
    return t;
}

} // namespace paimon::wallpaper
