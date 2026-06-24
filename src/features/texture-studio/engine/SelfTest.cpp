#include "SelfTest.hpp"

#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "LuminanceTinter.hpp"
#include "MaskBuilder.hpp"
#include "../data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

ImageBuffer makeSyntheticSprite() {
    // 16×16 sprite: black outline frame, green inner ring, black-fill core.
    constexpr int W = 16;
    constexpr int H = 16;
    ImageBuffer img(W, H);

    auto put = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        img.setAt(x, y, {r, g, b, 255});
    };

    // Outline ring (very dark grey, S~0, V~0.13).
    for (int x = 0; x < W; ++x) {
        put(x, 0, 0x22, 0x22, 0x22);
        put(x, H - 1, 0x22, 0x22, 0x22);
    }
    for (int y = 0; y < H; ++y) {
        put(0, y, 0x22, 0x22, 0x22);
        put(W - 1, y, 0x22, 0x22, 0x22);
    }

    // Green inner band (rows 1..H-2, cols 1..W-2 minus a 4×4 inner core).
    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            put(x, y, 0x3F, 0xCC, 0x2F);
        }
    }

    // Inner 4×4 black core.
    for (int y = 6; y < 10; ++y) {
        for (int x = 6; x < 10; ++x) {
            put(x, y, 0x0F, 0x0F, 0x0F);
        }
    }

    return img;
}

// Build the *ground-truth* masks for the synthetic sprite from its known
// geometry (not from the classifier). The PSNR check compares the pipeline's
// tinted output against the tint produced from these reference masks: a high
// PSNR proves the classifier + mask builder reproduced the intended roles.
MaskSet makeGroundTruthMasks(ImageBuffer const& sprite) {
    int W = sprite.width();
    int H = sprite.height();
    auto fresh = [&]() {
        MaskBuffer m;
        m.width  = W;
        m.height = H;
        m.data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0);
        return m;
    };
    MaskSet gt;
    gt.color1  = fresh();
    gt.color2  = fresh();
    gt.glow    = fresh();
    gt.outline = fresh();

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            bool border = (x == 0 || x == W - 1 || y == 0 || y == H - 1);
            bool core   = (x >= 6 && x < 10 && y >= 6 && y < 10);
            if (border)      gt.outline.setAt(x, y, 255);
            else if (core)   gt.color2.setAt(x, y, 255);
            else             gt.color1.setAt(x, y, 255);
        }
    }
    return gt;
}

// Peak signal-to-noise ratio (dB) between two equally-sized RGBA buffers.
// Returns a large sentinel (120 dB) when the images are bit-identical.
double computePsnr(ImageBuffer const& a, ImageBuffer const& b) {
    if (a.width() != b.width() || a.height() != b.height() || a.empty()) {
        return 0.0;
    }
    double mse = 0.0;
    std::size_t n = a.byteSize();
    auto const* pa = a.data();
    auto const* pb = b.data();
    for (std::size_t i = 0; i < n; ++i) {
        double d = static_cast<double>(pa[i]) - static_cast<double>(pb[i]);
        mse += d * d;
    }
    mse /= static_cast<double>(n);
    if (mse <= 1e-9) return 120.0;
    return 10.0 * std::log10((255.0 * 255.0) / mse);
}

}  // anonymous namespace

bool engineSelfTest() {
    log::info("[texture-studio] engineSelfTest: starting");
    auto sprite = makeSyntheticSprite();

    // 1) Clustering
    ClusteringOptions copts;
    copts.k = 4;  // expect ≤3 distinct colors but ask for 4 to test re-seeding
    auto clusters = ColorClustering::compute(sprite, copts);
    log::info("[texture-studio] selfTest: clusters={}, totalPixels={}, rejected={}",
        clusters.clusters.size(), clusters.totalPixels, clusters.rejected);

    bool ok = true;
    if (clusters.clusters.size() < 3) {
        log::error("[texture-studio] selfTest FAIL: expected ≥3 clusters, got {}", clusters.clusters.size());
        ok = false;
    }

    // 2) Classification
    auto classified = ClusterClassifier::classify(clusters, sprite);
    int outlineCount = 0, c1Count = 0, c2Count = 0, glowCount = 0;
    for (auto const& c : classified.clusters) {
        switch (c.role) {
            case ClusterRole::Outline: ++outlineCount; break;
            case ClusterRole::Color1:  ++c1Count;      break;
            case ClusterRole::Color2:  ++c2Count;      break;
            case ClusterRole::Glow:    ++glowCount;    break;
            default: break;
        }
        log::info("[texture-studio] selfTest: cluster RGB=({},{},{}) HSV=({:.0f},{:.2f},{:.2f}) "
                  "px={} border={:.2f} → role={}",
            c.source.r, c.source.g, c.source.b,
            c.source.h, c.source.s, c.source.v,
            c.source.pixelCount, c.source.borderRatio,
            static_cast<int>(c.role));
    }
    if (outlineCount == 0) {
        log::error("[texture-studio] selfTest FAIL: no Outline cluster found");
        ok = false;
    }
    if (c1Count == 0) {
        log::error("[texture-studio] selfTest FAIL: no Color1 cluster found");
        ok = false;
    }
    if (c2Count == 0) {
        log::error("[texture-studio] selfTest FAIL: no Color2 cluster found");
        ok = false;
    }

    // 3) Mask building — at minimum the C1 mask must be non-empty (the
    // green band has lots of pixels).
    auto masks = MaskBuilder::build(sprite, classified);
    int c1Coverage = 0, c2Coverage = 0, outlineCoverage = 0;
    for (auto v : masks.color1.data) if (v > 0) ++c1Coverage;
    for (auto v : masks.color2.data) if (v > 0) ++c2Coverage;
    for (auto v : masks.outline.data) if (v > 0) ++outlineCoverage;
    log::info("[texture-studio] selfTest: c1 mask coverage = {} pixels", c1Coverage);
    if (c1Coverage == 0) {
        log::error("[texture-studio] selfTest FAIL: c1 mask has zero coverage");
        ok = false;
    }
    if (c2Coverage == 0 || outlineCoverage == 0) {
        log::error("[texture-studio] selfTest FAIL: missing C2/outline coverage ({}/{})",
            c2Coverage, outlineCoverage);
        ok = false;
    }

    // 4) Tinting — pick C1=red, C2=blue, glow=white. The result should
    // have the green band recolored to a reddish hue.
    TintColors tc;
    tc.color1 = {255, 64, 64};
    tc.color2 = {64,  64, 255};
    tc.glow   = {255, 255, 255};
    auto tinted = LuminanceTinter::apply(sprite, masks, tc);

    // Verify: pick a known-green pixel (x=4, y=4) and check it's now reddish
    // (R > G and R > B).
    auto px = tinted.at(4, 4);
    log::info("[texture-studio] selfTest: tinted (4,4) = ({},{},{},{})", px.r, px.g, px.b, px.a);
    if (!(px.r > px.g && px.r > px.b)) {
        log::error("[texture-studio] selfTest FAIL: green pixel did not become red-dominant");
        ok = false;
    }
    if (px.a == 0) {
        log::error("[texture-studio] selfTest FAIL: tinted pixel lost its alpha");
        ok = false;
    }

    // The dark inner accent is Color2 and must become blue; this catches a
    // former global dark-pixel guard that accidentally preserved all dark
    // details, not just the outline.
    auto inner = tinted.at(7, 7);
    if (!(inner.b > inner.r && inner.b > inner.g)) {
        log::error("[texture-studio] selfTest FAIL: dark Color2 pixel did not become blue-dominant");
        ok = false;
    }

    // The silhouette outline is explicitly masked and must remain bit-exact.
    auto outline = tinted.at(0, 0);
    auto originalOutline = sprite.at(0, 0);
    if (outline.r != originalOutline.r || outline.g != originalOutline.g ||
        outline.b != originalOutline.b || outline.a != originalOutline.a) {
        log::error("[texture-studio] selfTest FAIL: outline changed");
        ok = false;
    }

    // 5) Precision metric. Compare the full pipeline output against the tint
    // produced from ground-truth masks. A high PSNR means the classifier +
    // mask builder reproduced the intended per-role assignment. Anything
    // above ~30 dB is visually indistinguishable for this palette.
    auto gtMasks = makeGroundTruthMasks(sprite);
    auto expected = LuminanceTinter::apply(sprite, gtMasks, tc);
    double psnr = computePsnr(tinted, expected);
    constexpr double kMinPsnr = 30.0;
    log::info("[texture-studio] selfTest: PSNR vs ground-truth = {:.2f} dB (min {:.0f})",
        psnr, kMinPsnr);
    if (psnr < kMinPsnr) {
        log::error("[texture-studio] selfTest FAIL: PSNR {:.2f} dB below {:.0f} dB "
                   "(classifier/mask mismatch vs ground truth)", psnr, kMinPsnr);
        ok = false;
    }

    log::info("[texture-studio] engineSelfTest: {}", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace paimon::texture_studio
