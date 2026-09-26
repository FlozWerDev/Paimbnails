#pragma once
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace paimon::scorecell {

// HSL-locked gradient: hue kept, sat/light clamped into a background-safe band.

namespace detail {
inline void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max({r, g, b});
    float mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) {
        h = 0.f;
        s = 0.f;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    h *= 60.f;
}

inline cocos2d::ccColor3B hslToRgb(float h, float s, float l) {
    h = std::fmod(h, 360.f);
    if (h < 0.f) h += 360.f;
    float r, g, b;
    if (s <= 0.f) {
        r = g = b = l;
    } else {
        auto hue2rgb = [](float p, float q, float t) {
            if (t < 0.f) t += 1.f;
            if (t > 1.f) t -= 1.f;
            if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
            if (t < 0.5f) return q;
            if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
            return p;
        };
        float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
        float p = 2.f * l - q;
        float hk = h / 360.f;
        r = hue2rgb(p, q, hk + 1.f / 3.f);
        g = hue2rgb(p, q, hk);
        b = hue2rgb(p, q, hk - 1.f / 3.f);
    }
    auto toByte = [](float v) -> GLubyte {
        return static_cast<GLubyte>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
    };
    return {toByte(r), toByte(g), toByte(b)};
}

inline std::pair<cocos2d::ccColor3B, cocos2d::ccColor3B>
harmonizePair(cocos2d::ccColor3B a, cocos2d::ccColor3B b) {
    float h1, s1, l1, h2, s2, l2;
    rgbToHsl(a.r / 255.f, a.g / 255.f, a.b / 255.f, h1, s1, l1);
    rgbToHsl(b.r / 255.f, b.g / 255.f, b.b / 255.f, h2, s2, l2);

    // Achromatic stops have no hue: neutrals stay neutral, only chromatic get vivid.
    bool c1 = s1 >= 0.08f;
    bool c2 = s2 >= 0.08f;
    s1 = c1 ? std::clamp(s1, 0.38f, 0.80f) : std::min(s1, 0.12f);
    s2 = c2 ? std::clamp(s2, 0.38f, 0.80f) : std::min(s2, 0.12f);
    l1 = std::clamp(l1, 0.30f, 0.62f);
    l2 = std::clamp(l2, 0.30f, 0.62f);

    // Near-analogous pair becomes a duo (A's hue sacred, only B moves); B still darkens on neutrals.
    float dh = std::fabs(h1 - h2);
    dh = std::min(dh, 360.f - dh);
    if (dh < 20.f) {
        if (c1 && c2) h2 = h1 + 28.f;
        // Push B darker so the gradient keeps a direction.
        l2 = std::clamp(l2 - 0.07f, 0.28f, 0.62f);
    }

    return {hslToRgb(h1, s1, l1), hslToRgb(h2, s2, l2)};
}
} // namespace detail

// Scorecell-scope alias for call sites outside detail.
inline std::pair<cocos2d::ccColor3B, cocos2d::ccColor3B>
designScoreGradient(cocos2d::ccColor3B a, cocos2d::ccColor3B b) {
    return detail::harmonizePair(a, b);
}

// Per-cell overlays above the gradient. CCLayerGradient IS-A CCLayerColor, so paimon IDs never demote behind.
inline void attachCellOverlays(cocos2d::CCNode* clip, cocos2d::CCSize cs) {
    if (!clip) return;
    if (cs.width <= 1.f || cs.height <= 1.f) return;
    if (auto* scrim = cocos2d::CCLayerGradient::create(
            cocos2d::ccc4(0, 0, 0, 95), cocos2d::ccc4(0, 0, 0, 0), ccp(1.f, 0.f))) {
        scrim->setContentSize(cs);
        scrim->setAnchorPoint({0.5f, 0.5f});
        scrim->ignoreAnchorPointForPosition(false);
        scrim->setPosition({cs.width / 2.f, cs.height / 2.f});
        scrim->setID("paimon-score-scrim"_spr);
        clip->addChild(scrim, 1);
    }
    if (auto* rscrim = cocos2d::CCLayerGradient::create(
            cocos2d::ccc4(0, 0, 0, 0), cocos2d::ccc4(0, 0, 0, 45), ccp(1.f, 0.f))) {
        rscrim->setContentSize(cs);
        rscrim->setAnchorPoint({0.5f, 0.5f});
        rscrim->ignoreAnchorPointForPosition(false);
        rscrim->setPosition({cs.width / 2.f, cs.height / 2.f});
        rscrim->setID("paimon-score-rscrim"_spr);
        clip->addChild(rscrim, 1);
    }
    if (auto* sheen = cocos2d::CCLayerGradient::create(
            cocos2d::ccc4(255, 255, 255, 16), cocos2d::ccc4(255, 255, 255, 0),
            ccp(0.f, -1.f))) {
        sheen->setContentSize(cs);
        sheen->setAnchorPoint({0.5f, 0.5f});
        sheen->ignoreAnchorPointForPosition(false);
        sheen->setPosition({cs.width / 2.f, cs.height / 2.f});
        sheen->setID("paimon-score-sheen"_spr);
        clip->addChild(sheen, 2);
    }
}

} // namespace paimon::scorecell
