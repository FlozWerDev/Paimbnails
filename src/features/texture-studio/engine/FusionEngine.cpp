#include "FusionEngine.hpp"
#include "SpritePreviewRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace paimon::texture_studio {

namespace {

inline float rgbDist(std::uint8_t const* a, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b) {
    float dr = static_cast<float>(a[0]) - static_cast<float>(r);
    float dg = static_cast<float>(a[1]) - static_cast<float>(g);
    float db = static_cast<float>(a[2]) - static_cast<float>(b);
    return std::sqrt(dr * dr + dg * dg + db * db);
}

// Luminance-tolerant colour distance: light/dark shades of the same hue
// (pale lime ↔ deeper green on GD buttons) stay closer than pure Euclidean RGB.
// Chroma is still strict so white rings / BPM glyphs stay out.
inline float colorDist(std::uint8_t const* p,
                       std::uint8_t sr, std::uint8_t sg, std::uint8_t sb) {
    float dr = static_cast<float>(p[0]) - static_cast<float>(sr);
    float dg = static_cast<float>(p[1]) - static_cast<float>(sg);
    float db = static_cast<float>(p[2]) - static_cast<float>(sb);
    float dL = 0.299f * dr + 0.587f * dg + 0.114f * db;
    float cr = dr - dL;
    float cg = dg - dL;
    float cb = db - dL;
    // Higher = gradients (top light / bottom dark) join more easily.
    constexpr float kLumaScale = 3.6f;
    return std::sqrt(
        (dL * dL) / (kLumaScale * kLumaScale) + cr * cr + cg * cg + cb * cb);
}

// Soft match for flood-fill: base distance, plus a gradient bridge that allows
// more luminance swing when chroma is still close (same family of greens).
inline bool colorMatches(std::uint8_t const* p,
                         std::uint8_t sr, std::uint8_t sg, std::uint8_t sb,
                         float limit) {
    float dr = static_cast<float>(p[0]) - static_cast<float>(sr);
    float dg = static_cast<float>(p[1]) - static_cast<float>(sg);
    float db = static_cast<float>(p[2]) - static_cast<float>(sb);
    float dL = 0.299f * dr + 0.587f * dg + 0.114f * db;
    float cr = dr - dL;
    float cg = dg - dL;
    float cb = db - dL;
    float chromaSq = cr * cr + cg * cg + cb * cb;
    constexpr float kLumaScale = 3.6f;
    float d = std::sqrt((dL * dL) / (kLumaScale * kLumaScale) + chromaSq);
    if (d <= limit) return true;
    float chroma = std::sqrt(chromaSq);
    // Same-hue gradient bridge: keep white/text out via chroma ceiling.
    if (chroma <= limit * 0.70f && std::fabs(dL) <= limit * 3.0f) return true;
    return false;
}

inline int luma(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (static_cast<int>(r) * 299 + static_cast<int>(g) * 587
            + static_cast<int>(b) * 114) / 1000;
}

inline std::uint8_t clampU8(int v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
}

inline float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

void sealInteriorHoles(MaskBuffer& mask, ImageBuffer const& sprite, int alphaCutoff) {
    int W = mask.width;
    int H = mask.height;
    if (W <= 0 || H <= 0 || mask.data.empty() || sprite.empty()) return;

    std::vector<std::uint8_t> dil(mask.data.size(), 0);
    auto const* srcM = mask.data.data();
    auto const* px = sprite.data();

    auto mAt = [&](int x, int y) -> std::uint8_t {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        return srcM[static_cast<std::size_t>(y) * W + x];
    };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::uint8_t v = 0;
            for (int dy = -1; dy <= 1 && !v; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (mAt(x + dx, y + dy)) { v = 255; break; }
                }
            }
            dil[static_cast<std::size_t>(y) * W + x] = v;
        }
    }

    std::vector<std::uint8_t> closed(mask.data.size(), 0);
    auto dAt = [&](int x, int y) -> std::uint8_t {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        return dil[static_cast<std::size_t>(y) * W + x];
    };
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::uint8_t v = 255;
            for (int dy = -1; dy <= 1 && v; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dAt(x + dx, y + dy)) { v = 0; break; }
                }
            }
            closed[static_cast<std::size_t>(y) * W + x] = v;
        }
    }

    int aCut = std::clamp(alphaCutoff, 0, 255);
    for (int i = 0, n = W * H; i < n; ++i) {
        if (srcM[static_cast<std::size_t>(i)]) {
            mask.data[static_cast<std::size_t>(i)] = 255;
            continue;
        }
        if (!closed[static_cast<std::size_t>(i)] || !dil[static_cast<std::size_t>(i)]) {
            mask.data[static_cast<std::size_t>(i)] = 0;
            continue;
        }
        if (px[static_cast<std::size_t>(i) * ImageBuffer::kBytesPerPixel + 3]
            < static_cast<std::uint8_t>(aCut)) {
            mask.data[static_cast<std::size_t>(i)] = 0;
            continue;
        }
        mask.data[static_cast<std::size_t>(i)] = 255;
    }
}

}  // namespace

std::vector<float> FusionEngine::softCoverage(MaskBuffer const& mask) {
    int W = mask.width;
    int H = mask.height;
    std::vector<float> soft(
        static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0.f);
    if (mask.empty()) return soft;

    auto const* m = mask.data.data();
    auto at = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0.f;
        return m[static_cast<std::size_t>(y) * W + x] / 255.f;
    };

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float sum = 0.f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    sum += at(x + dx, y + dy);
                }
            }
            float v = sum / 9.f;
            if (at(x, y) >= 1.f && v > 0.55f) v = 1.f;
            soft[static_cast<std::size_t>(y) * W + x] = clamp01(v);
        }
    }
    return soft;
}

ImageBuffer FusionEngine::buildStampCanvas(ImageBuffer const& texture,
                                           int frameW, int frameH,
                                           ImageTransform transform,
                                           MaskBuffer const* mask,
                                           int pixelOffsetX,
                                           int pixelOffsetY) {
    if (texture.empty() || frameW <= 0 || frameH <= 0) return ImageBuffer();
    transform.opacity = 255;
    // Fusion defaults to Fill so the texture covers the painted region
    // instead of letterboxing (Fit) and leaving the original colour visible.
    if (transform.isDefault()) {
        transform.fitMode = ImageFitMode::Fill;
    }

    int bx = 0, by = 0, bw = frameW, bh = frameH;
    if (mask && !mask->empty()
        && mask->width == frameW && mask->height == frameH) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        if (maskBounds(*mask, mx, my, mw, mh) && mw > 0 && mh > 0) {
            // 1px pad so soft coverage / AA at the mask edge still has texels.
            constexpr int kPad = 1;
            bx = std::max(0, mx - kPad);
            by = std::max(0, my - kPad);
            int x1 = std::min(frameW, mx + mw + kPad);
            int y1 = std::min(frameH, my + mh + kPad);
            bw = std::max(1, x1 - bx);
            bh = std::max(1, y1 - by);
        }
    }

    // Fit/Fill/Stretch relative to the painted region (mask AABB), not the
    // full sprite canvas — avoids "cropped" texture when only part of the
    // button is filled.
    ImageBuffer patch = SpritePreviewRenderer::renderCustomImage(
        texture, bw, bh, transform,
        static_cast<float>(pixelOffsetX), static_cast<float>(pixelOffsetY));
    if (patch.empty()) return ImageBuffer();

    if (bx == 0 && by == 0 && bw == frameW && bh == frameH) {
        return patch;
    }

    ImageBuffer canvas(frameW, frameH);
    canvas.blitOverwrite(bx, by, patch);
    return canvas;
}

void FusionEngine::applyCached(ImageBuffer& base,
                               std::vector<float> const& coverage,
                               ImageBuffer const& stampCanvas,
                               FusionApplyOptions const& options) {
    if (base.empty() || stampCanvas.empty() || coverage.empty()) return;
    if (base.width() != stampCanvas.width() || base.height() != stampCanvas.height()) return;
    if (coverage.size() != base.pixelCount()) return;

    int fw = base.width();
    int fh = base.height();
    int userOpacity = std::clamp(options.transform.opacity, 0, 255);
    float opMul = clamp01(options.opacity) * (userOpacity / 255.f);
    if (opMul <= 0.f) return;

    int offX = options.pixelOffsetX;
    int offY = options.pixelOffsetY;

    auto* baseData = base.data();
    auto const* texData = stampCanvas.data();
    std::size_t n = base.pixelCount();

    for (std::size_t i = 0; i < n; ++i) {
        float cov = coverage[i];
        if (cov <= 0.001f) continue;

        auto* bp = baseData + i * ImageBuffer::kBytesPerPixel;
        if (bp[3] == 0) continue;

        // Sample stamp with pixel shift (nearest). Out-of-bounds = transparent.
        int x = static_cast<int>(i % static_cast<std::size_t>(fw));
        int y = static_cast<int>(i / static_cast<std::size_t>(fw));
        int sx = x - offX;
        int sy = y - offY;
        if (sx < 0 || sy < 0 || sx >= fw || sy >= fh) continue;

        auto const* tp = texData
            + (static_cast<std::size_t>(sy) * fw + sx) * ImageBuffer::kBytesPerPixel;
        float texA = (static_cast<float>(tp[3]) / 255.f) * cov * opMul;
        if (texA <= 0.001f) continue;

        switch (options.blendMode) {
            case FusionBlendMode::Overlay: {
                float topA = texA;
                float baseA = bp[3] / 255.f;
                float outA = topA + baseA * (1.f - topA);
                if (outA <= 0.f) break;
                float invA = 1.f / outA;
                for (int c = 0; c < 3; ++c) {
                    float v = (tp[c] * topA + bp[c] * baseA * (1.f - topA)) * invA;
                    bp[c] = clampU8(static_cast<int>(std::lround(v)));
                }
                break;
            }
            case FusionBlendMode::MultiplyLuma: {
                int baseL = luma(bp[0], bp[1], bp[2]);
                float factor = std::max(0.08f, baseL / 255.f);
                int tr = clampU8(static_cast<int>(std::lround(tp[0] * factor)));
                int tg = clampU8(static_cast<int>(std::lround(tp[1] * factor)));
                int tb = clampU8(static_cast<int>(std::lround(tp[2] * factor)));
                float inv = 1.f - texA;
                bp[0] = clampU8(static_cast<int>(std::lround(tr * texA + bp[0] * inv)));
                bp[1] = clampU8(static_cast<int>(std::lround(tg * texA + bp[1] * inv)));
                bp[2] = clampU8(static_cast<int>(std::lround(tb * texA + bp[2] * inv)));
                break;
            }
            case FusionBlendMode::Replace:
            default: {
                if (texA >= 0.97f && tp[3] >= 250) {
                    bp[0] = tp[0]; bp[1] = tp[1]; bp[2] = tp[2];
                } else {
                    float inv = 1.f - texA;
                    bp[0] = clampU8(static_cast<int>(std::lround(tp[0] * texA + bp[0] * inv)));
                    bp[1] = clampU8(static_cast<int>(std::lround(tp[1] * texA + bp[1] * inv)));
                    bp[2] = clampU8(static_cast<int>(std::lround(tp[2] * texA + bp[2] * inv)));
                }
                break;
            }
        }
    }
}

MaskBuffer FusionEngine::floodFill(ImageBuffer const& sprite,
                                   int seedX, int seedY,
                                   int colorRadius,
                                   int alphaCutoff,
                                   int expandRadius) {
    MaskBuffer out;
    if (sprite.empty()) return out;

    int W = sprite.width();
    int H = sprite.height();
    if (seedX < 0 || seedY < 0 || seedX >= W || seedY >= H) return out;

    auto const* seedPx = sprite.atRef(seedX, seedY);
    int aCut = std::clamp(alphaCutoff, 0, 255);
    if (seedPx[3] < static_cast<std::uint8_t>(aCut)) {
        return out;
    }

    std::uint8_t sr = seedPx[0], sg = seedPx[1], sb = seedPx[2];
    // UI radius 0..255. Soft band ~55% wider to absorb AA + gradient fringes.
    float tol = static_cast<float>(std::clamp(colorRadius, 0, 255));
    float softTol = tol * 1.55f;

    out.width  = W;
    out.height = H;
    out.data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);

    std::vector<std::uint8_t> seen(
        static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);

    std::vector<int> stack;
    stack.reserve(static_cast<std::size_t>(W * H) / 4 + 64);
    int seedIdx = seedY * W + seedX;
    stack.push_back(seedIdx);
    seen[static_cast<std::size_t>(seedIdx)] = 1;

    auto const* src = sprite.data();
    auto* mask = out.data.data();

    auto matches = [&](std::uint8_t const* p, float limit) -> bool {
        if (p[3] < static_cast<std::uint8_t>(aCut)) return false;
        return colorMatches(p, sr, sg, sb, limit);
    };

    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();

        auto const* p = src + static_cast<std::size_t>(idx) * ImageBuffer::kBytesPerPixel;
        if (!matches(p, softTol)) continue;

        mask[static_cast<std::size_t>(idx)] = 255;

        int y = idx / W;
        int x = idx % W;
        bool core = matches(p, tol);

        for (int dy = -1; dy <= 1; ++dy) {
            int yy = y + dy;
            if (yy < 0 || yy >= H) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int xx = x + dx;
                if (xx < 0 || xx >= W) continue;
                int ni = yy * W + xx;
                if (seen[static_cast<std::size_t>(ni)]) continue;

                auto const* np = src
                    + static_cast<std::size_t>(ni) * ImageBuffer::kBytesPerPixel;
                float limit = core ? softTol : tol;
                if (!matches(np, limit)) continue;

                seen[static_cast<std::size_t>(ni)] = 1;
                stack.push_back(ni);
            }
        }
    }

    sealInteriorHoles(out, sprite, aCut);
    // Expand only into same-colour neighbours (soft band), never white rings
    // or glyphs that fail the colour test.
    expandMask(out, sprite, expandRadius, sr, sg, sb,
               static_cast<int>(std::lround(softTol)), aCut);
    return out;
}

void FusionEngine::expandMask(MaskBuffer& mask,
                              ImageBuffer const& sprite,
                              int radius,
                              std::uint8_t seedR, std::uint8_t seedG, std::uint8_t seedB,
                              int colorRadius,
                              int alphaCutoff) {
    int r = std::clamp(radius, 0, 12);
    if (r <= 0 || mask.empty() || sprite.empty()) return;
    if (mask.width != sprite.width() || mask.height != sprite.height()) return;

    int W = mask.width;
    int H = mask.height;
    int aCut = std::clamp(alphaCutoff, 0, 255);
    float limit = static_cast<float>(std::clamp(colorRadius, 0, 255));
    auto const* px = sprite.data();

    std::vector<std::uint8_t> cur = mask.data;
    std::vector<std::uint8_t> nxt(cur.size(), 0);

    for (int pass = 0; pass < r; ++pass) {
        std::fill(nxt.begin(), nxt.end(), 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                std::size_t i = static_cast<std::size_t>(y) * W + x;
                if (cur[i]) {
                    nxt[i] = 255;
                    continue;
                }
                auto const* p = px + i * ImageBuffer::kBytesPerPixel;
                if (p[3] < static_cast<std::uint8_t>(aCut)) continue;
                // Reject white outline / letters / other hues.
                if (colorDist(p, seedR, seedG, seedB) > limit) continue;

                bool neighbour = false;
                for (int dy = -1; dy <= 1 && !neighbour; ++dy) {
                    int yy = y + dy;
                    if (yy < 0 || yy >= H) continue;
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int xx = x + dx;
                        if (xx < 0 || xx >= W) continue;
                        if (cur[static_cast<std::size_t>(yy) * W + xx]) {
                            neighbour = true;
                            break;
                        }
                    }
                }
                if (neighbour) nxt[i] = 255;
            }
        }
        cur.swap(nxt);
    }
    mask.data.swap(cur);
}

void FusionEngine::orMask(MaskBuffer& dst, MaskBuffer const& src) {
    if (src.empty()) return;
    if (dst.empty()) {
        dst = src;
        return;
    }
    if (dst.width != src.width || dst.height != src.height) return;
    if (dst.data.size() != src.data.size()) return;

    auto* d = dst.data.data();
    auto const* s = src.data.data();
    std::size_t n = dst.data.size();
    for (std::size_t i = 0; i < n; ++i) {
        d[i] = static_cast<std::uint8_t>(d[i] | s[i]);
    }
}

bool FusionEngine::maskBounds(MaskBuffer const& mask,
                              int& outX, int& outY, int& outW, int& outH) {
    if (mask.empty()) return false;
    int W = mask.width;
    int H = mask.height;
    int minX = W, minY = H, maxX = -1, maxY = -1;
    auto const* m = mask.data.data();
    for (int y = 0; y < H; ++y) {
        auto const* row = m + static_cast<std::size_t>(y) * W;
        for (int x = 0; x < W; ++x) {
            if (row[x] == 0) continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY) return false;
    outX = minX;
    outY = minY;
    outW = maxX - minX + 1;
    outH = maxY - minY + 1;
    return true;
}

void FusionEngine::apply(ImageBuffer& base,
                         MaskBuffer const& mask,
                         ImageBuffer const& texture,
                         FusionApplyOptions const& options) {
    if (base.empty() || mask.empty() || texture.empty()) return;
    if (base.width() != mask.width || base.height() != mask.height) return;

    ImageTransform local = options.transform;
    if (local.isDefault()) {
        local.fitMode = ImageFitMode::Fill;
    }
    // Bake integer placement into the source sampling pass. Shifting a
    // pre-rendered canvas would lose pixels clipped by the mask bounds.
    auto stamp = buildStampCanvas(texture, base.width(), base.height(), local, &mask,
                                  options.pixelOffsetX, options.pixelOffsetY);
    if (stamp.empty()) return;
    auto coverage = softCoverage(mask);
    auto bakedOptions = options;
    bakedOptions.pixelOffsetX = 0;
    bakedOptions.pixelOffsetY = 0;
    applyCached(base, coverage, stamp, bakedOptions);
}

ImageBuffer FusionEngine::applyCopy(ImageBuffer const& base,
                                    MaskBuffer const& mask,
                                    ImageBuffer const& texture,
                                    FusionApplyOptions const& options) {
    ImageBuffer out(base);
    apply(out, mask, texture, options);
    return out;
}

}  // namespace paimon::texture_studio
