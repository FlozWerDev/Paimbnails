#include "SelfTest.hpp"

#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "LuminanceTinter.hpp"
#include "MaskBuilder.hpp"
#include "../data/ImageBuffer.hpp"

#include <Geode/Geode.hpp>

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

    // 3) Mask building — at minimum the C1 mask must be non-empty (the
    // green band has lots of pixels).
    auto masks = MaskBuilder::build(sprite, classified);
    int c1Coverage = 0;
    for (auto v : masks.color1.data) if (v > 0) ++c1Coverage;
    log::info("[texture-studio] selfTest: c1 mask coverage = {} pixels", c1Coverage);
    if (c1Coverage == 0) {
        log::error("[texture-studio] selfTest FAIL: c1 mask has zero coverage");
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

    log::info("[texture-studio] engineSelfTest: {}", ok ? "PASS" : "FAIL");
    return ok;
}

}  // namespace paimon::texture_studio
