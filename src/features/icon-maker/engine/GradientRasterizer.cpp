#include "GradientRasterizer.hpp"

#include <algorithm>
#include <cmath>

namespace paimon::icon_maker {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::uint8_t lerpByte(std::uint8_t a, std::uint8_t b, float t) {
    return static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(std::lround(a + (b - a) * t)), 0, 255));
}

}  // anonymous namespace

cocos2d::ccColor4B GradientRasterizer::sample(GradientSpec const& spec, float t) {
    if (spec.stops.empty()) return {255, 255, 255, 255};
    t = std::clamp(t, 0.f, 1.f);

    // Stops are kept sorted by pos (enforced on deserialize and by the editor).
    if (t <= spec.stops.front().pos) return spec.stops.front().color;
    if (t >= spec.stops.back().pos) return spec.stops.back().color;

    for (std::size_t i = 1; i < spec.stops.size(); ++i) {
        auto const& a = spec.stops[i - 1];
        auto const& b = spec.stops[i];
        if (t > b.pos) continue;
        float span = b.pos - a.pos;
        float local = span > 1e-6f ? (t - a.pos) / span : 0.f;
        return {
            lerpByte(a.color.r, b.color.r, local),
            lerpByte(a.color.g, b.color.g, local),
            lerpByte(a.color.b, b.color.b, local),
            lerpByte(a.color.a, b.color.a, local),
        };
    }
    return spec.stops.back().color;
}

float GradientRasterizer::paramAt(GradientSpec const& spec, float x, float y,
                                  float w, float h) {
    if (w <= 0.f || h <= 0.f) return 0.f;

    if (spec.kind == GradientKind::Radial) {
        float cx = spec.centerX * w;
        float cy = spec.centerY * h;
        float halfDiag = 0.5f * std::sqrt(w * w + h * h);
        float radius = std::max(spec.radius * halfDiag, 1e-3f);
        float dx = x - cx;
        float dy = y - cy;
        return std::sqrt(dx * dx + dy * dy) / radius;
    }

    // Linear: project onto the angle axis, normalized so the region's extent
    // along that axis maps to 0..1. angle 0 = left→right, 90 = top→bottom.
    float rad = spec.angleDeg * (kPi / 180.f);
    float dirX = std::cos(rad);
    float dirY = std::sin(rad);
    float proj = (x - w * 0.5f) * dirX + (y - h * 0.5f) * dirY;
    float extent = std::fabs(w * dirX) + std::fabs(h * dirY);
    if (extent < 1e-3f) return 0.f;
    return proj / extent + 0.5f;
}

texture_studio::ImageBuffer GradientRasterizer::rasterize(int w, int h,
                                                          GradientSpec const& spec) {
    texture_studio::ImageBuffer out(w, h);
    if (out.empty()) return out;

    auto* dst = out.data();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto c = sample(spec, paramAt(spec,
                static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                static_cast<float>(w), static_cast<float>(h)));
            auto* p = dst + (static_cast<std::size_t>(y) * w + x) * 4;
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
        }
    }
    return out;
}

}  // namespace paimon::icon_maker
