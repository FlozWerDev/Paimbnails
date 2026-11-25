#include "../src/features/backgrounds/services/SceneLayout.hpp"
#include "../src/features/backgrounds/services/WallpaperLayout.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace paimon::wallpaper;

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) <= eps; }

void checkAnchor(std::string_view horizontal, std::string_view vertical, float x, float y) {
    auto const anchor = anchorForAlignment(horizontal, vertical);
    assert(anchor.x == x);
    assert(anchor.y == y);
}

// Real workshop scenes are authored 0-based: a 3840x2160 background sits at
// origin (1920, 1080). Painted at that origin it must cover a 16:9 window.
// The old renderer added half the projection again and pushed every layer off
// screen, leaving only the clear colour.
void checkZeroBasedSceneCoversViewport() {
    constexpr float sceneW = 3840.f, sceneH = 2160.f;
    auto const layout = fitWallpaper(sceneW, sceneH, 1366.f, 768.f, "cover");
    float const scale = layout.width / sceneW;
    auto const world = sceneLocalTransform(1920.f, 1080.f, 0.f, 1.f, 1.f);
    float left = 0.f, bottom = 0.f, right = 0.f, top = 0.f;
    sceneApply(world, -sceneW * 0.5f, -sceneH * 0.5f, left, bottom);
    sceneApply(world, sceneW * 0.5f, sceneH * 0.5f, right, top);
    assert(near(left, 0.f) && near(bottom, 0.f) && near(right, sceneW) && near(top, sceneH));
    assert(layout.left + left * scale <= 0.5f);
    assert(layout.left + right * scale >= 1366.f - 0.5f);
    assert(layout.bottom + bottom * scale <= 0.5f);
    assert(layout.bottom + top * scale >= 768.f - 0.5f);
}

// Children are centred on their parent and inherit its rotation and scale.
void checkChildInheritsParent() {
    auto const parent = sceneLocalTransform(1000.f, 500.f, 3.14159265f / 2.f, 2.f, 2.f);
    auto const child = sceneMultiply(parent, sceneLocalTransform(10.f, 0.f, 0.f, 1.f, 1.f));
    // +10 on the child's x lands +20 on the parent's rotated (up) axis.
    assert(near(child.tx, 1000.f, 1e-2f));
    assert(near(child.ty, 520.f, 1e-2f));
}

// Wallpaper Engine angles are counter-clockwise radians; cocos rotates
// clockwise in degrees on each axis.
void checkDecomposition() {
    auto const quarter = decomposeSceneAffine(sceneLocalTransform(5.f, 6.f, -0.78539816f, 3.f, 3.f));
    assert(near(quarter.rotationX, 45.f) && near(quarter.rotationY, 45.f));
    assert(near(quarter.scaleX, 3.f) && near(quarter.scaleY, 3.f));
    assert(near(quarter.x, 5.f) && near(quarter.y, 6.f));

    // a mirrored layer (negative x scale) keeps its size and flips one axis.
    auto const mirrored = decomposeSceneAffine(sceneLocalTransform(0.f, 0.f, 0.f, -1.f, 1.f));
    assert(near(mirrored.scaleX, 1.f) && near(mirrored.scaleY, 1.f));
    assert(near(std::abs(mirrored.rotationY), 180.f) && near(mirrored.rotationX, 0.f));
}

void checkAutoProjection() {
    assert(sceneExtentFromEdges(1920.f) == 1920.f);
    assert(sceneExtentFromEdges(0.f) == 0.f);
    assert(sceneExtentFromEdges(-5.f) == 0.f);
}

// Parallax used to be inverted: depth 1 (background) moved and depth 0 moved
// the most. Wallpaper Engine scales movement by the depth.
void checkParallaxDirection() {
    assert(parallaxOffset(0.f, 1.f, 1.f) == 0.f);
    assert(parallaxOffset(1.f, 1.f, 1.f) == 25.f);
    assert(parallaxOffset(0.5f, 1.f, 1.f) == 12.5f);
    assert(parallaxOffset(1.f, -1.f, 1.f) == -25.f);
    assert(parallaxOffset(1.f, 1.f, 0.f) == 0.f);
    assert(parallaxOffset(0.9f, 1.f, 1.f) > parallaxOffset(0.1f, 1.f, 1.f));
}

} // namespace

int main() {
    checkAnchor("center", "center", 0.5f, 0.5f);
    checkAnchor("", "", 0.5f, 0.5f);
    checkAnchor("left", "top", 0.f, 1.f);
    checkAnchor("right", "bottom", 1.f, 0.f);
    // Wallpaper Engine may pack both axes into one string.
    checkAnchor("topleft", "", 0.f, 1.f);
    checkAnchor("bottomright", "", 1.f, 0.f);
    // case-insensitive.
    checkAnchor("LEFT", "TOP", 0.f, 1.f);

    checkZeroBasedSceneCoversViewport();
    checkChildInheritsParent();
    checkDecomposition();
    checkAutoProjection();
    checkParallaxDirection();

    std::printf("PASS: scene layout (0-based origin, hierarchy, radians, parallax)\n");
    return 0;
}
