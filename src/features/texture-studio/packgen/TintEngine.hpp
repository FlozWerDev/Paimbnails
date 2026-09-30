#pragma once
// packgen v2 tint kernel, bit-exact with tintmath; speed from hoisted invariants, not new math.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace paimon::texture_studio::packgen {

// 0.30/0.59/0.11 rec.601 weights, matching tintmath::luminance601.
inline float luminance601f(float r, float g, float b) {
    return 0.30f * r + 0.59f * g + 0.11f * b;
}

inline std::uint8_t clampByteFast(int v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

// same expression as per-pixel a/255 once: bit-identical blend via table lookup.
struct AlphaLut {
    float v[256];
    static AlphaLut make() {
        AlphaLut lut{};
        for (int i = 0; i < 256; ++i) {
            lut.v[i] = static_cast<float>(i) / 255.0f;
        }
        return lut;
    }
};

// everything about one tint role resolved once, outside the pixel loop.
struct PrecomputedTint {
    float tintR = 0.0f, tintG = 0.0f, tintB = 0.0f;
    float brightness = 160.0f;
    float saturation = 1.0f;
    float contrast = 0.0f;
    float contrastGain = 1.0f;
    bool applySat = false;
    bool applyContrast = false;

    static PrecomputedTint make(std::uint8_t tr, std::uint8_t tg, std::uint8_t tb,
                                float brightnessF, float sat, float con) {
        PrecomputedTint p;
        p.tintR = static_cast<float>(tr);
        p.tintG = static_cast<float>(tg);
        p.tintB = static_cast<float>(tb);
        p.brightness = std::clamp(brightnessF, 1.0f, 1000.0f);
        p.saturation = std::clamp(sat, 0.0f, 3.0f);
        p.contrast = std::clamp(con, -1.0f, 1.0f);
        p.contrastGain = 1.0f + p.contrast;
        p.applySat = (p.saturation != 1.0f);
        p.applyContrast = (p.contrast != 0.0f);
        return p;
    }
};

// same op order as tintmath::tintbyluminance.
inline void tintPixelFast(std::uint8_t srcR, std::uint8_t srcG, std::uint8_t srcB,
                          PrecomputedTint const& t,
                          std::uint8_t& outR, std::uint8_t& outG, std::uint8_t& outB) {
    float lum = luminance601f(static_cast<float>(srcR),
                              static_cast<float>(srcG),
                              static_cast<float>(srcB));
    float factor = lum / t.brightness;

    float fR = std::clamp(t.tintR * factor, 0.0f, 255.0f);
    float fG = std::clamp(t.tintG * factor, 0.0f, 255.0f);
    float fB = std::clamp(t.tintB * factor, 0.0f, 255.0f);

    if (t.applySat) {
        float luma = 0.30f * fR + 0.59f * fG + 0.11f * fB;
        fR = luma + (fR - luma) * t.saturation;
        fG = luma + (fG - luma) * t.saturation;
        fB = luma + (fB - luma) * t.saturation;
    }
    if (t.applyContrast) {
        fR = (fR - 127.5f) * t.contrastGain + 127.5f;
        fG = (fG - 127.5f) * t.contrastGain + 127.5f;
        fB = (fB - 127.5f) * t.contrastGain + 127.5f;
    }

    outR = clampByteFast(static_cast<int>(std::lround(fR)));
    outG = clampByteFast(static_cast<int>(std::lround(fG)));
    outB = clampByteFast(static_cast<int>(std::lround(fB)));
}

// same op order as tintmath::overlaypixel / replacepixel.
inline void blendPixelFast(std::uint8_t& baseR, std::uint8_t& baseG,
                           std::uint8_t& baseB, std::uint8_t& baseA,
                           std::uint8_t ovR, std::uint8_t ovG, std::uint8_t ovB,
                           std::uint8_t ovA, AlphaLut const& lut, bool replace) {
    if (ovA == 0) return;
    if (replace || ovA == 255) {
        baseR = ovR;
        baseG = ovG;
        baseB = ovB;
        baseA = std::max(baseA, ovA);
        return;
    }
    float alpha = lut.v[ovA];
    float invA = 1.0f - alpha;
    baseR = clampByteFast(static_cast<int>(std::lround(ovR * alpha + baseR * invA)));
    baseG = clampByteFast(static_cast<int>(std::lround(ovG * alpha + baseG * invA)));
    baseB = clampByteFast(static_cast<int>(std::lround(ovB * alpha + baseB * invA)));
    baseA = std::max(baseA, ovA);
}

// fused multi-role kernel mirroring luminancetinter::apply. dst starts as a src copy; null rows mean role absent.
inline std::size_t tintStackImage(std::uint8_t const* src, std::uint8_t* dst,
                                  int w, int h,
                                  std::uint8_t const* maskC1,
                                  std::uint8_t const* maskC2,
                                  std::uint8_t const* maskDetail,
                                  std::uint8_t const* maskGlow,
                                  PrecomputedTint const& c1,
                                  PrecomputedTint const& c2,
                                  PrecomputedTint const& detail,
                                  PrecomputedTint const& glow,
                                  bool hasDetail, bool glowReplace,
                                  int darkThreshold,
                                  AlphaLut const& lut) {
    if (!src || !dst || w <= 0 || h <= 0) return 0;
    bool hasC1 = (maskC1 != nullptr);
    bool hasC2 = (maskC2 != nullptr);
    bool hasGlow = (maskGlow != nullptr);
    bool hasDet = hasDetail && (maskDetail != nullptr);
    float darkF = static_cast<float>(darkThreshold);
    std::size_t tinted = 0;

    std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t o = i * 4;
        std::uint8_t srcA = src[o + 3];
        if (srcA == 0) continue;
        std::uint8_t srcR = src[o], srcG = src[o + 1], srcB = src[o + 2];

        if (darkThreshold > 0 &&
            luminance601f(static_cast<float>(srcR),
                          static_cast<float>(srcG),
                          static_cast<float>(srcB)) < darkF) {
            continue;
        }

        std::uint8_t baseR = srcR, baseG = srcG, baseB = srcB, baseA = srcA;
        bool touched = false;

        if (hasC1 && maskC1[i] > 0) {
            std::uint8_t tR, tG, tB;
            tintPixelFast(srcR, srcG, srcB, c1, tR, tG, tB);
            blendPixelFast(baseR, baseG, baseB, baseA, tR, tG, tB, maskC1[i], lut, false);
            touched = true;
        }
        if (hasC2 && maskC2[i] > 0) {
            std::uint8_t tR, tG, tB;
            tintPixelFast(srcR, srcG, srcB, c2, tR, tG, tB);
            blendPixelFast(baseR, baseG, baseB, baseA, tR, tG, tB, maskC2[i], lut, false);
            touched = true;
        }
        if (hasDet && maskDetail[i] > 0) {
            std::uint8_t tR, tG, tB;
            tintPixelFast(srcR, srcG, srcB, detail, tR, tG, tB);
            blendPixelFast(baseR, baseG, baseB, baseA, tR, tG, tB, maskDetail[i], lut, false);
            touched = true;
        }
        if (hasGlow && maskGlow[i] > 0) {
            std::uint8_t tR, tG, tB;
            tintPixelFast(srcR, srcG, srcB, glow, tR, tG, tB);
            blendPixelFast(baseR, baseG, baseB, baseA, tR, tG, tB, maskGlow[i], lut, glowReplace);
            touched = true;
        }

        if (touched) {
            dst[o] = baseR; dst[o + 1] = baseG;
            dst[o + 2] = baseB; dst[o + 3] = baseA;
            ++tinted;
        }
    }
    return tinted;
}

// one overlay pass over row band [y0, y1), mirroring overlaytinter::applyone with the top-left overlap rule.
inline void applyOverlayBand(std::uint8_t* dst, int dstW,
                             std::uint8_t const* ov, int ovW,
                             int y0, int y1, int bandW,
                             PrecomputedTint const& spec,
                             AlphaLut const& lut, bool replace) {
    for (int y = y0; y < y1; ++y) {
        std::uint8_t* dRow = dst + static_cast<std::size_t>(y) * dstW * 4;
        std::uint8_t const* oRow = ov + static_cast<std::size_t>(y) * ovW * 4;
        for (int x = 0; x < bandW; ++x) {
            std::size_t doff = static_cast<std::size_t>(x) * 4;
            std::uint8_t oa = oRow[doff + 3];
            if (oa == 0) continue;
            std::uint8_t tR, tG, tB;
            tintPixelFast(oRow[doff], oRow[doff + 1], oRow[doff + 2], spec, tR, tG, tB);
            blendPixelFast(dRow[doff], dRow[doff + 1], dRow[doff + 2], dRow[doff + 3],
                           tR, tG, tB, oa, lut, replace);
        }
    }
}

}  // namespace paimon::texture_studio::packgen
