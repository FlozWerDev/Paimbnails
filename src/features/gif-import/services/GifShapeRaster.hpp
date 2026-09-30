#pragma once

#include "../GifImportTypes.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace paimon::gifimport {

// cache rotation once; repeated trigonometry dominates shape queries.
struct ShapeXform {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float cosine = 1.f;
    float sine = 0.f;
    float extentX = 0.f;
    float extentY = 0.f;
    PrimitiveKind kind = PrimitiveKind::Block;
    StampMask const* mask = nullptr;

    bool contains(float px, float py) const {
        if (width <= 0.f || height <= 0.f) return false;
        float const dx = px - x;
        float const dy = py - y;
        float const localX = dx * cosine + dy * sine;
        float const localY = -dx * sine + dy * cosine;

        if (kind == PrimitiveKind::Circle) {
            float const nx = localX / (width * 0.5f);
            float const ny = localY / (height * 0.5f);
            return nx * nx + ny * ny <= 1.f;
        }
        if (kind == PrimitiveKind::Triangle || kind == PrimitiveKind::WideTriangle) {
            float const u = localX / width + 0.5f;
            float const v = localY / height + 0.5f;
            return u >= 0.f && v >= 0.f && u <= 1.f && v <= 1.f && u + v <= 1.f;
        }
        if (kind == PrimitiveKind::Stamp) {
            if (!mask || mask->empty()) return false;
            float const u = localX / width + 0.5f;
            float const v = localY / height + 0.5f;
            if (u < 0.f || v < 0.f || u >= 1.f || v >= 1.f) return false;
            int const cellX = std::min(
                mask->width - 1, static_cast<int>(u * mask->width));
            int const cellY = std::min(
                mask->height - 1, static_cast<int>(v * mask->height));
            return mask->coverage[
                static_cast<std::size_t>(cellY) * mask->width + cellX] >= 128;
        }
        return std::abs(localX) <= width * 0.5f && std::abs(localY) <= height * 0.5f;
    }
};

ShapeXform xformOf(Primitive const& object);
ShapeXform xformOf(Primitive const& object, std::vector<PlanStamp> const& stamps);
std::vector<ShapeXform> xformsOf(std::vector<Primitive> const& objects);

// bounding-box cells, clipped to the grid. empty box (max < min) when outside.
std::array<int, 4> xformBox(ShapeXform const& shape, int width, int height);

// center sampling misses peaks extending less than half a cell into a lower color.
bool shapeStaysInside(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height
);

// allow small strip bevels and caps; rejecting them can produce a worse outline.
float shapeSpill(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height
);

// x span a figure can touch in a row. deliberately a hair wide both sides,
// so outside it the point reads as rejected without asking.
bool xformSpan(ShapeXform const& shape, float y, float& fromX, float& toX);

// walks the samples a figure covers on a `scale`-samples-per-cell grid, in
// sample coords. returning true from `fn` cuts.
template <typename Fn>
bool forEachSample(ShapeXform const& shape, int width, int height, int scale, Fn&& fn) {
    auto const box = xformBox(shape, width, height);
    if (box[2] < box[0] || box[3] < box[1]) return false;
    int const lowX = box[0] * scale;
    int const highX = (box[2] + 1) * scale - 1;
    float const step = 1.f / static_cast<float>(scale);
    for (int y = box[1] * scale; y <= (box[3] + 1) * scale - 1; ++y) {
        float const sampleY = (static_cast<float>(y) + 0.5f) * step;
        float spanFrom = 0.f;
        float spanTo = 0.f;
        if (!xformSpan(shape, sampleY, spanFrom, spanTo)) continue;
        int const first = std::max(
            lowX, static_cast<int>(std::floor(spanFrom * scale - 0.5f)));
        int const last = std::min(
            highX, static_cast<int>(std::ceil(spanTo * scale - 0.5f)));
        for (int x = first; x <= last; ++x) {
            if (!shape.contains((static_cast<float>(x) + 0.5f) * step, sampleY)) continue;
            if (fn(x, y)) return true;
        }
    }
    return false;
}

} // namespace paimon::gifimport
