#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace paimon::glass {

struct GlassPoint {
    float x = 0.f;
    float y = 0.f;
};

// integer framebuffer rectangle in GL convention (origin at the bottom left).
struct GlassRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    int right() const { return x + w; }
    int top() const { return y + h; }
    bool contains(GlassRect const& other) const {
        return !empty() && !other.empty() && other.x >= x && other.y >= y &&
            other.right() <= right() && other.top() <= top();
    }
    bool intersects(GlassRect const& other) const {
        return !empty() && !other.empty() && other.x < right() && x < other.right() &&
            other.y < top() && y < other.top();
    }
    bool operator==(GlassRect const&) const = default;
};

inline GlassRect unite(GlassRect const& a, GlassRect const& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    int const x0 = std::min(a.x, b.x);
    int const y0 = std::min(a.y, b.y);
    int const x1 = std::max(a.right(), b.right());
    int const y1 = std::max(a.top(), b.top());
    return {x0, y0, x1 - x0, y1 - y0};
}

// grows `rect` by `pad` pixels and keeps it inside `bounds` (the viewport).
inline GlassRect padAndClamp(GlassRect const& rect, int pad, GlassRect const& bounds) {
    if (rect.empty() || bounds.empty()) return {};
    pad = std::max(pad, 0);
    int const x0 = std::max(rect.x - pad, bounds.x);
    int const y0 = std::max(rect.y - pad, bounds.y);
    int const x1 = std::min(rect.right() + pad, bounds.right());
    int const y1 = std::min(rect.top() + pad, bounds.top());
    if (x1 <= x0 || y1 <= y0) return {};
    return {x0, y0, x1 - x0, y1 - y0};
}

// framebuffer position of a node-local point: origin + x * axisX + y * axisY.
struct FramebufferAxes {
    GlassPoint origin;
    GlassPoint axisX;
    GlassPoint axisY;
};

// `mvp` is the column-major matrix the plate is drawn with and `viewport`
// the current GL_VIEWPORT. Works for the screen and for render targets,
// because both define the same mapping the rasterizer uses. Nodes live
// on z = 0, so even the default 3D projection stays affine here.
inline std::optional<FramebufferAxes> framebufferAxesFor(
    float const* mvp, GlassRect const& viewport, float width, float height) {
    if (!mvp || viewport.empty() || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.f || height <= 0.f) {
        return std::nullopt;
    }
    auto project = [&](float x, float y, GlassPoint& out) {
        float const cx = mvp[0] * x + mvp[4] * y + mvp[12];
        float const cy = mvp[1] * x + mvp[5] * y + mvp[13];
        float const cw = mvp[3] * x + mvp[7] * y + mvp[15];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) ||
            std::abs(cw) < 1e-6f) {
            return false;
        }
        out.x = static_cast<float>(viewport.x) + (cx / cw * 0.5f + 0.5f) * static_cast<float>(viewport.w);
        out.y = static_cast<float>(viewport.y) + (cy / cw * 0.5f + 0.5f) * static_cast<float>(viewport.h);
        return std::isfinite(out.x) && std::isfinite(out.y);
    };
    GlassPoint origin, right, top;
    if (!project(0.f, 0.f, origin) || !project(width, 0.f, right) || !project(0.f, height, top)) {
        return std::nullopt;
    }
    return FramebufferAxes{
        origin,
        {(right.x - origin.x) / width, (right.y - origin.y) / width},
        {(top.x - origin.x) / height, (top.y - origin.y) / height},
    };
}

// framebuffer pixels per local unit (geometric mean of both axes).
inline float pixelsPerUnit(FramebufferAxes const& axes) {
    float const sx = std::hypot(axes.axisX.x, axes.axisX.y);
    float const sy = std::hypot(axes.axisY.x, axes.axisY.y);
    float const scale = std::sqrt(sx * sy);
    return std::isfinite(scale) ? scale : 0.f;
}

// pixel-aligned bounding box of the local rectangle [x0, x1] x [y0, y1].
inline GlassRect boundsOf(FramebufferAxes const& axes, float x0, float y0, float x1, float y1) {
    auto at = [&](float x, float y) {
        return GlassPoint{axes.origin.x + x * axes.axisX.x + y * axes.axisY.x,
                          axes.origin.y + x * axes.axisX.y + y * axes.axisY.y};
    };
    GlassPoint const corners[] = {at(x0, y0), at(x1, y0), at(x0, y1), at(x1, y1)};
    float minX = corners[0].x, maxX = corners[0].x;
    float minY = corners[0].y, maxY = corners[0].y;
    for (auto const& corner : corners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y)) return {};
        minX = std::min(minX, corner.x);
        maxX = std::max(maxX, corner.x);
        minY = std::min(minY, corner.y);
        maxY = std::max(maxY, corner.y);
    }
    // keep the float-to-int conversion defined for absurd transforms.
    constexpr float kLimit = 1.0e6f;
    minX = std::clamp(std::floor(minX), -kLimit, kLimit);
    minY = std::clamp(std::floor(minY), -kLimit, kLimit);
    maxX = std::clamp(std::ceil(maxX), -kLimit, kLimit);
    maxY = std::clamp(std::ceil(maxY), -kLimit, kLimit);
    return {static_cast<int>(minX), static_cast<int>(minY),
            static_cast<int>(maxX - minX), static_cast<int>(maxY - minY)};
}

// texture allocations grow in steps so animated popups do not reallocate
// every frame while they scale in.
inline int bucketUp(int value, int step = 64) {
    step = std::max(step, 1);
    value = std::max(value, 1);
    return (value + step - 1) / step * step;
}

// one dual-kawase level: half the size, rounded up, never zero.
inline int halfExtent(int value) {
    return std::max(1, (value + 1) / 2);
}

} // namespace paimon::glass
