#include "IconColorService.hpp"
#include "IconColorMath.hpp"
#include "IconConfigStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/CCDirector.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::icons {

IconColorService& IconColorService::get() {
    static IconColorService instance;
    return instance;
}

float IconColorService::globalTime() {
    if (auto* d = CCDirector::sharedDirector()) {
        // Per-call accumulator backed by a static so value monotonically increases.
        static float acc = 0.0f;
        static float lastTotalFrames = -1.0f;
        const float frames = static_cast<float>(d->getTotalFrames());
        if (lastTotalFrames < 0.0f || frames < lastTotalFrames) {
            lastTotalFrames = frames;
            return acc;
        }
        const float dt = (frames - lastTotalFrames) * d->getAnimationInterval();
        lastTotalFrames = frames;
        acc += dt;
        return acc;
    }
    return 0.0f;
}

IconColorTriple IconColorService::readPlayerColors() const {
    IconColorTriple t;
    auto* gm = GameManager::get();
    if (!gm) return t;
    t.primary   = gm->colorForIdx(gm->getPlayerColor());
    t.secondary = gm->colorForIdx(gm->getPlayerColor2());
    t.glow      = gm->colorForIdx(gm->getPlayerGlowColor());
    t.hasGlow   = gm->getPlayerGlow();
    return t;
}

IconColorTriple IconColorService::resolve(IconDescriptor const& desc) const {
    return resolve(desc, globalTime());
}

IconColorTriple IconColorService::resolve(IconDescriptor const& desc, float nowSeconds) const {
    auto const& cfg = IconConfigStore::get().config();
    auto base = readPlayerColors();

    switch (cfg.mode) {
        case ColorMode::Player:      return resolvePlayer(base);
        case ColorMode::CustomRGB:   return resolveCustomRGB(cfg);
        case ColorMode::HueShift:    return resolveHueShift(base, cfg);
        case ColorMode::SatBoost:    return resolveSatBoost(base, cfg);
        case ColorMode::RandomStable:return resolveRandom(desc, cfg);
        case ColorMode::Rainbow:     return resolveRainbow(desc, cfg, nowSeconds);
        case ColorMode::Gradient:    return resolveGradient(desc, cfg);
        case ColorMode::PerGamemode: return resolvePerGamemode(desc, base, cfg);
        case ColorMode::Inverted:    return resolveInverted(base);
        case ColorMode::Monochrome:  return resolveMonochrome(cfg);
    }
    return base;
}

IconColorTriple IconColorService::resolvePlayer(IconColorTriple const& base) const {
    return base;
}

IconColorTriple IconColorService::resolveCustomRGB(PaimonIconConfig const& cfg) const {
    return {cfg.custom1, cfg.custom2, cfg.customGlow, true};
}

IconColorTriple IconColorService::resolveHueShift(IconColorTriple const& base, PaimonIconConfig const& cfg) const {
    IconColorTriple t = base;
    t.primary   = math::rotateHue(base.primary,   cfg.hueShiftDegrees);
    t.secondary = math::rotateHue(base.secondary, cfg.hueShiftDegrees);
    t.glow      = math::rotateHue(base.glow,      cfg.hueShiftDegrees);
    return t;
}

IconColorTriple IconColorService::resolveSatBoost(IconColorTriple const& base, PaimonIconConfig const& cfg) const {
    IconColorTriple t = base;
    t.primary   = math::scaleSV(base.primary,   cfg.saturationMul, cfg.brightnessMul);
    t.secondary = math::scaleSV(base.secondary, cfg.saturationMul, cfg.brightnessMul);
    t.glow      = math::scaleSV(base.glow,      cfg.saturationMul, cfg.brightnessMul);
    return t;
}

IconColorTriple IconColorService::resolveRandom(IconDescriptor const& desc, PaimonIconConfig const& cfg) const {
    const std::uint64_t seed =
        (static_cast<std::uint64_t>(desc.unlockTypeRaw) << 32) ^
         static_cast<std::uint64_t>(desc.iconID);
    const std::uint64_t h1 = math::splitMix64(seed);
    const std::uint64_t h2 = math::splitMix64(seed ^ 0xA5A5A5A5'5A5A5A5AULL);
    const std::uint64_t h3 = math::splitMix64(seed ^ 0x123456789ABCDEFULL);

    auto pick = [&](std::uint64_t h, RandomPalette pal) -> cocos2d::ccColor3B {
        const float hue = math::hashToFloat01(h) * 360.0f;
        switch (pal) {
            case RandomPalette::Vibrant:    return math::fromHSV({hue, 1.00f, 1.00f});
            case RandomPalette::Pastel:     return math::fromHSV({hue, 0.45f, 1.00f});
            case RandomPalette::Neon:       return math::fromHSV({hue, 0.85f, 1.00f});
            case RandomPalette::Earthy:     return math::fromHSV({std::fmod(hue, 60.0f) + 20.0f, 0.55f, 0.75f});
            case RandomPalette::Monoschemed: {
                auto baseHsv = math::toHSV(cfg.monochromeBase);
                baseHsv.h = math::wrapHue(baseHsv.h + (math::hashToFloat01(h) - 0.5f) * 30.0f);
                return math::fromHSV(baseHsv);
            }
        }
        return math::fromHSV({hue, 1.0f, 1.0f});
    };

    IconColorTriple t;
    t.primary   = pick(h1, cfg.randomPalette);
    t.secondary = pick(h2, cfg.randomPalette);
    t.glow      = pick(h3, cfg.randomPalette);
    t.hasGlow   = true;
    return t;
}

IconColorTriple IconColorService::resolveRainbow(IconDescriptor const& desc, PaimonIconConfig const& cfg, float t) const {
    // Phase offset per (unlockType, iconID) so rows sweep instead of flashing together.
    const float baseHue = std::fmod(t * cfg.rainbowSpeed * 60.0f, 360.0f);
    const float phase   = static_cast<float>((desc.iconID * 17 + desc.unlockTypeRaw * 53) % 360);
    const float spread  = cfg.rainbowSpread;

    IconColorTriple out;
    out.primary   = math::fromHSV({math::wrapHue(baseHue + phase),                  1.0f, 1.0f});
    out.secondary = math::fromHSV({math::wrapHue(baseHue + phase + spread),         0.85f, 0.95f});
    out.glow      = math::fromHSV({math::wrapHue(baseHue + phase + spread * 2.0f),  1.0f, 1.0f});
    out.hasGlow   = true;
    return out;
}

IconColorTriple IconColorService::resolveGradient(IconDescriptor const& desc, PaimonIconConfig const& cfg) const {
    const float total = std::max(1, desc.totalCount);
    const float progress = std::clamp(static_cast<float>(desc.displayIndex) / (total - 1.0f), 0.0f, 1.0f);
    cocos2d::ccColor3B mixed = math::lerp(cfg.gradientStart, cfg.gradientEnd, progress);
    IconColorTriple out;
    out.primary   = mixed;
    out.secondary = math::scaleSV(mixed, 1.0f, 0.7f);
    out.glow      = math::scaleSV(mixed, 1.0f, 1.2f);
    out.hasGlow   = true;
    return out;
}

IconColorTriple IconColorService::resolvePerGamemode(IconDescriptor const& desc, IconColorTriple const& base, PaimonIconConfig const& cfg) const {
    if (desc.gamemodeIndex < 0 || desc.gamemodeIndex >= static_cast<int>(cfg.perMode.size())) {
        return base;
    }
    auto const& slot = cfg.perMode[desc.gamemodeIndex];
    if (!slot) return base;
    IconColorTriple out;
    out.primary   = slot->color1;
    out.secondary = slot->color2;
    out.glow      = slot->glow;
    out.hasGlow   = true;
    return out;
}

IconColorTriple IconColorService::resolveInverted(IconColorTriple const& base) const {
    IconColorTriple t = base;
    t.primary   = math::complement(base.primary);
    t.secondary = math::complement(base.secondary);
    t.glow      = math::complement(base.glow);
    return t;
}

IconColorTriple IconColorService::resolveMonochrome(PaimonIconConfig const& cfg) const {
    auto baseHsv = math::toHSV(cfg.monochromeBase);
    IconColorTriple out;
    out.primary   = cfg.monochromeBase;
    {
        auto h = baseHsv;
        h.v = math::clamp01(h.v * 0.65f);
        h.s = math::clamp01(h.s * 0.85f);
        out.secondary = math::fromHSV(h);
    }
    {
        auto h = baseHsv;
        h.v = math::clamp01(h.v * 1.15f);
        out.glow = math::fromHSV(h);
    }
    out.hasGlow = true;
    return out;
}

}  // namespace paimon::icons
