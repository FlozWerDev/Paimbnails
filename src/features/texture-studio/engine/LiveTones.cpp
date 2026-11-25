#include "LiveTones.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::texture_studio {

namespace {

struct Hsl {
    float h = 0.f, s = 0.f, l = 0.f;
};

Hsl toHsl(cocos2d::ccColor3B c) {
    float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
    float maxC = std::max({r, g, b});
    float minC = std::min({r, g, b});
    Hsl out;
    out.l = (maxC + minC) * .5f;
    float d = maxC - minC;
    if (d < 1e-6f) return out;
    out.s = out.l > .5f ? d / (2.f - maxC - minC) : d / (maxC + minC);
    float h;
    if (maxC == r) h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (maxC == g) h = (b - r) / d + 2.f;
    else h = (r - g) / d + 4.f;
    out.h = h * 60.f;
    return out;
}

float hueChannel(float p, float q, float t) {
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
    if (t < .5f) return q;
    if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
    return p;
}

std::uint8_t toByte(float v) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v * 255.f)), 0, 255));
}

cocos2d::ccColor3B fromHsl(Hsl c) {
    float h = std::fmod(c.h, 360.f);
    if (h < 0.f) h += 360.f;
    h /= 360.f;
    float s = std::clamp(c.s, 0.f, 1.f);
    float l = std::clamp(c.l, 0.f, 1.f);
    if (s <= 0.f) return {toByte(l), toByte(l), toByte(l)};
    float q = l < .5f ? l * (1.f + s) : l + s - l * s;
    float p = 2.f * l - q;
    return {toByte(hueChannel(p, q, h + 1.f / 3.f)), toByte(hueChannel(p, q, h)),
            toByte(hueChannel(p, q, h - 1.f / 3.f))};
}

// moves a color along hue/lightness. A shade that cannot go darker (or
// lighter) flips direction so it still separates from its base color.
cocos2d::ccColor3B shade(cocos2d::ccColor3B color, float hueShift, float light, float satScale = 1.f) {
    auto hsl = toHsl(color);
    hsl.h += hueShift;
    hsl.s = std::clamp(hsl.s * satScale, 0.f, 1.f);
    if (light >= 0.f) {
        hsl.l = hsl.l > .85f ? hsl.l * (1.f - light * .7f) : hsl.l + (1.f - hsl.l) * light;
    } else {
        hsl.l = hsl.l < .18f ? hsl.l + (1.f - hsl.l) * -light : hsl.l * (1.f + light);
    }
    return fromHsl(hsl);
}

cocos2d::ccColor3B mix(cocos2d::ccColor3B a, cocos2d::ccColor3B b, float t) {
    auto lerp = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(x + (y - x) * t)), 0, 255));
    };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b)};
}

float luma(cocos2d::ccColor3B c) {
    return .30f * c.r + .59f * c.g + .11f * c.b;
}

}  // namespace

cocos2d::ccColor3B autoSurfaceTone(cocos2d::ccColor3B, cocos2d::ccColor3B secondary) {
    // muted, darker secondary: panels recede behind the buttons on them.
    return shade(secondary, 0.f, -.30f, .55f);
}

cocos2d::ccColor3B autoBackgroundTone(cocos2d::ccColor3B primary, cocos2d::ccColor3B secondary) {
    // backgrounds show this tone at full brightness at the top of the gradient,
    // so it sits darker than the buttons drawn over it (never lighter: a
    // black palette keeps a dark scene, warmed slightly by the secondary).
    auto hsl = toHsl(primary);
    hsl.l *= .65f;
    hsl.s *= .85f;
    auto tone = fromHsl(hsl);
    if (luma(tone) < 30.f) tone = mix(tone, secondary, .2f);
    return tone;
}

LiveTonePalette buildLiveTones(LiveToneInputs const& in) {
    float k = std::isfinite(in.spread) ? std::clamp(in.spread, 0.f, 1.f) : .6f;
    auto const p = in.primary;
    auto const s = in.secondary;
    auto const middle = mix(p, s, .5f);
    std::uint8_t gray = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(std::lround((luma(p) + luma(s)) * .5f)), 0, 255));

    LiveTonePalette tones{};
    auto set = [&](LiveTone tone, cocos2d::ccColor3B color) {
        tones[static_cast<std::size_t>(tone)] = color;
    };
    set(LiveTone::Primary, p);
    set(LiveTone::Secondary, s);
    set(LiveTone::Blue, shade(s, 18.f * k, -.32f * k));
    set(LiveTone::Pink, shade(mix(p, middle, k), 30.f * k, .30f * k));
    set(LiveTone::Red, shade(p, -25.f * k, -.38f * k));
    set(LiveTone::Surface, in.autoSurface ? autoSurfaceTone(p, s) : in.surface);
    // gray UI must not vanish into a gray/white primary.
    auto neutral = mix({gray, gray, gray}, p, .15f);
    float dr = static_cast<float>(neutral.r) - p.r;
    float dg = static_cast<float>(neutral.g) - p.g;
    float db = static_cast<float>(neutral.b) - p.b;
    if (dr * dr + dg * dg + db * db < 60.f * 60.f) {
        neutral = shade(neutral, 0.f, luma(p) > 128.f ? -.22f : .22f);
    }
    set(LiveTone::Neutral, neutral);
    set(LiveTone::Background, in.autoBackground ? autoBackgroundTone(p, s) : in.background);
    return tones;
}

char const* liveToneName(LiveTone tone) {
    switch (tone) {
        case LiveTone::Primary:    return "Primary";
        case LiveTone::Secondary:  return "Secondary";
        case LiveTone::Blue:       return "Deep";
        case LiveTone::Pink:       return "Light";
        case LiveTone::Red:        return "Strong";
        case LiveTone::Surface:    return "Surface";
        case LiveTone::Neutral:    return "Neutral";
        case LiveTone::Background: return "Background";
        default:                   return "";
    }
}

char const* liveToneSource(LiveTone tone) {
    switch (tone) {
        case LiveTone::Primary:    return "Green";
        case LiveTone::Secondary:  return "Cyan";
        case LiveTone::Blue:       return "Blue";
        case LiveTone::Pink:       return "Pink";
        case LiveTone::Red:        return "Red";
        case LiveTone::Surface:    return "Brown";
        case LiveTone::Neutral:    return "Gray";
        case LiveTone::Background: return "Scene";
        default:                   return "";
    }
}

cocos2d::ccColor3B liveTintColor(cocos2d::ccColor3B source, cocos2d::ccColor3B tone,
                                 int brightness, float saturation, float contrast) {
    float lum = luma(source);
    float b = static_cast<float>(std::max(brightness, 1));
    float lx, ly;
    if (lum <= b) {
        lx = lum / b;
        ly = 0.f;
    } else {
        float t = std::clamp((lum - b) / std::max(255.f - b, 1.f), 0.f, 1.f);
        t = t * t * (3.f - 2.f * t);
        lx = 1.f - t;
        ly = 255.f * t;
    }
    float f[3] = {tone.r * lx + ly, tone.g * lx + ly, tone.b * lx + ly};
    if (saturation != 1.f) {
        float l = .30f * f[0] + .59f * f[1] + .11f * f[2];
        for (auto& c : f) c = l + (c - l) * saturation;
    }
    if (contrast != 0.f) {
        for (auto& c : f) c = (c - 127.5f) * (1.f + contrast) + 127.5f;
    }
    auto out = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::floor(v + .5f)), 0, 255));
    };
    return {out(f[0]), out(f[1]), out(f[2])};
}

}  // namespace paimon::texture_studio
