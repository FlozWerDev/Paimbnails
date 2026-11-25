#include "../src/features/dynamic-ambient/services/AmbientMath.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

std::vector<std::uint8_t> makeGrid(
    int w, int h,
    float bgR, float bgG, float bgB,
    float spotU, float spotV, float radius,
    float spotR, float spotG, float spotB
) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 255);
    for (int y = 0; y < h; ++y) {
        float const v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
        for (int x = 0; x < w; ++x) {
            float const u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w);
            float const d = std::hypot(u - spotU, v - spotV);
            float const wgt = std::exp(-(d * d) / std::max(1e-4f, radius * radius));
            float const r = std::clamp(bgR + (spotR - bgR) * wgt, 0.f, 1.f);
            float const g = std::clamp(bgG + (spotG - bgG) * wgt, 0.f, 1.f);
            float const b = std::clamp(bgB + (spotB - bgB) * wgt, 0.f, 1.f);
            std::size_t const idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4u;
            rgba[idx + 0] = static_cast<std::uint8_t>(std::round(r * 255.f));
            rgba[idx + 1] = static_cast<std::uint8_t>(std::round(g * 255.f));
            rgba[idx + 2] = static_cast<std::uint8_t>(std::round(b * 255.f));
            rgba[idx + 3] = 255;
        }
    }
    return rgba;
}

} // namespace

int main() {
    using namespace paimon::ambient;

    // 1. Warm top-left sun over deep night blue sky.
    auto const sunGrid = makeGrid(
        24, 14,
        0.04f, 0.14f, 0.36f,
        0.16f, 0.86f, 0.18f,
        1.00f, 0.92f, 0.45f
    );
    auto const sunState = analyzeAmbientGrid(sunGrid.data(), 24, 14);
    assert(sunState.fromCapture);
    assert(sunState.lightX < 0.32f);
    assert(sunState.lightY > 0.70f);
    assert(sunState.keyColor.r > sunState.keyColor.b);
    assert(sunState.shadowColor.b > sunState.shadowColor.r);
    assert(sunState.directionality > 0.45f);
    assert(sunState.palettePrimary.b > sunState.palettePrimary.r);
    assert(sunState.paletteSecondary.r > sunState.paletteSecondary.b);
    assert(sunState.paletteAccent.r > sunState.paletteAccent.b);
    assert(sunState.hasZones);

    auto const topLeftLocal = sampleLocalAmbientColor(sunState, 0.16f, 0.86f);
    auto const bottomRightLocal = sampleLocalAmbientColor(sunState, 0.85f, 0.15f);
    assert(topLeftLocal.r > bottomRightLocal.r);
    assert(bottomRightLocal.b > topLeftLocal.b);

    // 2. Warm coral sunset over deep indigo horizon -> warm Primary, cool Secondary.
    auto const sunsetGrid = makeGrid(
        24, 14,
        0.72f, 0.24f, 0.28f,
        0.35f, 0.25f, 0.28f,
        0.16f, 0.28f, 0.68f
    );
    auto const sunsetState = analyzeAmbientGrid(sunsetGrid.data(), 24, 14);
    assert(sunsetState.palettePrimary.r > sunsetState.palettePrimary.b);
    assert(sunsetState.paletteSecondary.b > sunsetState.paletteSecondary.r);

    // 2. Neon cyan bottom-right glow over dark violet backdrop.
    auto const neonGrid = makeGrid(
        24, 14,
        0.16f, 0.06f, 0.22f,
        0.84f, 0.18f, 0.16f,
        0.25f, 0.92f, 1.00f
    );
    auto const neonState = analyzeAmbientGrid(neonGrid.data(), 24, 14);
    assert(neonState.fromCapture);
    assert(neonState.lightX > 0.68f);
    assert(neonState.lightY < 0.32f);
    assert(neonState.keyColor.b > neonState.keyColor.r);
    assert(neonState.palettePrimary.b > 0.40f || neonState.paletteSecondary.b > 0.40f);
    assert(std::isfinite(neonState.paletteSurface.r) && neonState.paletteSurface.r >= 0.f && neonState.paletteSurface.r <= 1.f);
    assert(neonState.hasZones);
    auto const neonCorner = sampleLocalAmbientColor(neonState, 0.84f, 0.18f);
    auto const neonFar = sampleLocalAmbientColor(neonState, 0.12f, 0.85f);
    assert(neonCorner.b > neonFar.b || neonCorner.g > neonFar.g);

    // 3. Flat low-contrast background stays softly lit with low directionality.
    auto const flatGrid = makeGrid(
        24, 14,
        0.24f, 0.28f, 0.34f,
        0.50f, 0.50f, 0.30f,
        0.25f, 0.29f, 0.35f
    );
    auto const flatState = analyzeAmbientGrid(flatGrid.data(), 24, 14);
    assert(flatState.fromCapture);
    assert(flatState.directionality <= 0.30f);

    // 4. Node light direction from bottom-right button toward top-left sun.
    auto const btnLight = computeNodeLightDir(sunState, 0.78f, 0.22f, 16.f / 9.f);
    assert(btnLight.dirX < -0.45f);
    assert(btnLight.dirY > 0.25f);
    float const unitLen = std::hypot(btnLight.dirX, btnLight.dirY);
    assert(std::abs(unitLen - 1.f) < 1e-3f);
    assert(btnLight.proximityBoost >= 0.78f && btnLight.proximityBoost <= 1.32f);

    // Near-light center remains finite and normalized.
    auto const onLight = computeNodeLightDir(sunState, sunState.lightX, sunState.lightY, 16.f / 9.f);
    assert(std::isfinite(onLight.dirX) && std::isfinite(onLight.dirY));
    assert(std::abs(std::hypot(onLight.dirX, onLight.dirY) - 1.f) < 1e-3f);

    // 5. Smoothing snaps when cur has no capture, then converges smoothly.
    AmbientLightState blank;
    auto const snapped = smoothAmbientState(blank, neonState, 0.016f);
    assert(std::abs(snapped.lightX - neonState.lightX) < 1e-5f);
    assert(snapped.fromCapture);

    auto const mid = smoothAmbientState(sunState, neonState, 0.08f);
    assert(mid.lightX > sunState.lightX && mid.lightX < neonState.lightX);
    assert(mid.lightY < sunState.lightY && mid.lightY > neonState.lightY);

    // 6. Settings sanitization handles NaN and out-of-bounds values.
    AmbientSettings dirty;
    dirty.intensity = std::numeric_limits<float>::quiet_NaN();
    dirty.rimStrength = 99.f;
    dirty.shadowStrength = -5.f;
    dirty.tintStrength = 4.f;
    dirty.paletteStrength = -2.f;
    auto const clean = sanitizeAmbientSettings(dirty);
    AmbientSettings const defaults;
    assert(std::abs(clean.intensity - defaults.intensity) < 1e-5f);
    assert(std::abs(clean.rimStrength - 1.5f) < 1e-5f);
    assert(std::abs(clean.shadowStrength - 0.f) < 1e-5f);
    assert(std::abs(clean.tintStrength - 1.f) < 1e-5f);
    assert(std::abs(clean.paletteStrength - 0.f) < 1e-5f);

    std::cout << "dynamic_ambient_regression: ok\n";
    return 0;
}
