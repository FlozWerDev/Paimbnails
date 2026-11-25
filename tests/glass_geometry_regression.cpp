#include "../src/features/liquid-glass/services/GlassGeometry.hpp"

#include <cassert>
#include <cmath>
#include <limits>

using namespace paimon::glass;

namespace {

bool near(float a, float b) { return std::abs(a - b) < 0.0001f; }

// Column-major orthographic projection of [0, w] x [0, h] onto clip space.
void ortho(float w, float h, float out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = 0.f;
    out[0] = 2.f / w;
    out[5] = 2.f / h;
    out[10] = -1.f;
    out[12] = -1.f;
    out[13] = -1.f;
    out[15] = 1.f;
}

} // namespace

int main() {
    // A 569x320 point scene drawn into a letterboxed 1280x720 viewport.
    float mvp[16];
    ortho(569.f, 320.f, mvp);
    GlassRect const viewport{40, 0, 1200, 720};
    auto axes = framebufferAxesFor(mvp, viewport, 100.f, 50.f);
    assert(axes);
    assert(near(axes->origin.x, 40.f) && near(axes->origin.y, 0.f));
    assert(near(axes->axisX.x, 1200.f / 569.f) && near(axes->axisX.y, 0.f));
    assert(near(axes->axisY.x, 0.f) && near(axes->axisY.y, 720.f / 320.f));
    assert(near(pixelsPerUnit(*axes), std::sqrt(1200.f / 569.f * 720.f / 320.f)));

    // A node translated by (100, 60) and scaled by 0.5 lands where the
    // rasterizer puts it.
    float node[16];
    ortho(569.f, 320.f, node);
    node[12] += node[0] * 100.f;
    node[13] += node[5] * 60.f;
    node[0] *= 0.5f;
    node[5] *= 0.5f;
    axes = framebufferAxesFor(node, viewport, 80.f, 40.f);
    assert(axes);
    assert(near(axes->origin.x, 40.f + 100.f * 1200.f / 569.f));
    assert(near(axes->origin.y, 60.f * 720.f / 320.f));
    assert(near(axes->axisX.x, 0.5f * 1200.f / 569.f));
    auto bounds = boundsOf(*axes, 0.f, 0.f, 80.f, 40.f);
    assert(bounds.x == static_cast<int>(std::floor(axes->origin.x)));
    assert(bounds.w >= static_cast<int>(80.f * axes->axisX.x));

    // Rotation keeps the real corners: a 90 degree turn swaps the axes.
    float rotated[16];
    ortho(569.f, 320.f, rotated);
    float const sx = rotated[0];
    float const sy = rotated[5];
    rotated[0] = 0.f;
    rotated[1] = sy;
    rotated[4] = -sx;
    rotated[5] = 0.f;
    axes = framebufferAxesFor(rotated, viewport, 10.f, 10.f);
    assert(axes && near(axes->axisX.x, 0.f) && axes->axisX.y > 0.f && axes->axisY.x < 0.f);
    bounds = boundsOf(*axes, 0.f, 0.f, 10.f, 10.f);
    assert(bounds.right() <= viewport.x + 1 && bounds.w > 0 && bounds.h > 0);

    // Degenerate input never produces a mapping.
    assert(!framebufferAxesFor(nullptr, viewport, 1.f, 1.f));
    assert(!framebufferAxesFor(mvp, {}, 1.f, 1.f));
    assert(!framebufferAxesFor(mvp, viewport, 0.f, 1.f));
    assert(!framebufferAxesFor(mvp, viewport, std::numeric_limits<float>::quiet_NaN(), 1.f));
    float collapsed[16];
    ortho(569.f, 320.f, collapsed);
    collapsed[15] = 0.f;
    assert(!framebufferAxesFor(collapsed, viewport, 1.f, 1.f));
    FramebufferAxes huge{{0.f, 0.f}, {1.0e30f, 0.f}, {0.f, 1.f}};
    auto clampedBounds = boundsOf(huge, 0.f, 0.f, 1.f, 1.f);
    assert(clampedBounds.w > 0 && clampedBounds.right() <= 1000000);
    FramebufferAxes bad{{std::numeric_limits<float>::infinity(), 0.f}, {1.f, 0.f}, {0.f, 1.f}};
    assert(boundsOf(bad, 0.f, 0.f, 1.f, 1.f).empty());

    // Copy regions: padded, kept inside the viewport, united across plates.
    GlassRect const plate{100, 100, 50, 20};
    auto padded = padAndClamp(plate, 16, viewport);
    assert(padded == (GlassRect{84, 84, 82, 52}));
    assert(padded.contains(plate) && !plate.contains(padded));
    auto edge = padAndClamp({30, -10, 30, 30}, 16, viewport);
    assert(edge.x == viewport.x && edge.y == 0 && edge.right() == 76 && edge.top() == 36);
    assert(padAndClamp({-500, -500, 10, 10}, 16, viewport).empty());
    assert(padAndClamp({}, 16, viewport).empty());
    auto both = unite(plate, {300, 40, 10, 10});
    assert(both == (GlassRect{100, 40, 210, 80}));
    assert(unite({}, plate) == plate && unite(plate, {}) == plate);
    assert(plate.intersects({140, 110, 100, 100}));
    assert(!plate.intersects({150, 100, 10, 10})); // touching edges do not overlap
    assert(!GlassRect{}.contains(plate) && !plate.contains(GlassRect{}));

    // Allocations grow in buckets; every blur level keeps at least a texel.
    assert(bucketUp(1) == 64 && bucketUp(64) == 64 && bucketUp(65) == 128);
    assert(bucketUp(0, 32) == 32 && bucketUp(33, 32) == 64);
    assert(halfExtent(1) == 1 && halfExtent(2) == 1 && halfExtent(3) == 2 && halfExtent(1001) == 501);
}
