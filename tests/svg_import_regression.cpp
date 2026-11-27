#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../src/features/gif-import/services/GifSvgImporter.hpp"
#include "../src/features/gif-import/services/GifPrimitiveSerializer.hpp"
#include "../src/features/gif-import/services/ColorSpace.cpp"
#include "../src/features/gif-import/services/GifParallel.cpp"
#include "../src/features/gif-import/services/GifShapeRaster.cpp"
#include "../src/features/gif-import/services/GifVectorMath.cpp"
#include "../src/features/gif-import/services/GifMotionPlanner.cpp"
#include "../src/features/gif-import/services/GifStampCatalog.cpp"
#include "../src/features/gif-import/services/GifArtVectorizer.cpp"
#include "../src/features/gif-import/services/GifSvgImporter.cpp"

using namespace paimon::gifimport;

namespace {

bool check(bool cond, char const* label) {
    if (!cond) std::cerr << "FAIL: " << label << '\n';
    else std::cout << "PASS: " << label << '\n';
    return cond;
}

bool testDirectRectAndCircle() {
    std::string const svg = R"svg(
        <svg viewBox="0 0 200 200" xmlns="http://www.w3.org/2000/svg">
            <rect x="0" y="0" width="200" height="200" fill="#ffffff"/>
            <rect x="20" y="30" width="80" height="50" fill="#e03040"/>
            <circle cx="140" cy="140" r="35" fill="#2060e0"/>
        </svg>
    )svg";
    SvgOptions opt;
    opt.dimension = 100;
    opt.maxColors = 8;
    auto res = buildSvgPlan(svg, opt);
    bool ok = static_cast<bool>(res) &&
        res.plan.totalObjects == 3 &&
        res.plan.blockObjects == 2 &&
        res.plan.circleObjects == 1 &&
        res.plan.triangleObjects == 0 &&
        res.plan.palette.size() == 3;
    return check(ok, "direct rect + circle -> 3 exact GD primitives");
}

bool testOccludedShapePruned() {
    std::string const svg = R"svg(
        <svg viewBox="0 0 200 200" xmlns="http://www.w3.org/2000/svg">
            <rect x="0" y="0" width="200" height="200" fill="#ffffff"/>
            <rect x="50" y="50" width="60" height="60" fill="#ff0000"/>
            <rect x="20" y="20" width="160" height="160" fill="#00aa44"/>
        </svg>
    )svg";
    SvgOptions opt;
    opt.dimension = 100;
    auto res = buildSvgPlan(svg, opt);
    bool ok = static_cast<bool>(res) &&
        res.plan.totalObjects == 2 &&
        res.stats.occludedPruned >= 1;
    return check(ok, "100% occluded shape pruned cleanly");
}

bool testPlanarPartitionHoleEliminationAndSameColorMerge() {
    // Planar partition: white outer frame with hole filled by two adjacent near-identical red halves.
    std::string const svg = R"svg(
        <svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
            <path fill="#ffffff" d="M 0 0 L 100 0 L 100 100 L 0 100 Z M 20 20 L 20 80 L 50 80 L 80 80 L 80 20 L 50 20 Z"/>
            <path fill="#d02030" d="M 20 20 L 50 20 L 50 80 L 20 80 Z"/>
            <path fill="#d12131" d="M 50 20 L 80 20 L 80 80 L 50 80 Z"/>
        </svg>
    )svg";
    SvgOptions opt;
    opt.dimension = 100;
    opt.maxColors = 8;
    auto res = buildSvgPlan(svg, opt);
    bool ok = static_cast<bool>(res) &&
        res.stats.planarPartition &&
        res.plan.palette.size() == 2 &&
        res.plan.totalObjects == 2 &&
        res.plan.blockObjects == 2 &&
        res.plan.triangleObjects == 0;
    return check(ok, "planar partition fuses near-identical red halves + backdrop -> 2 blocks");
}

bool testLayeredPathWithHoleTriangulation() {
    // Non-planar single path with an open transparent hole -> exercises hole-bridging earcut.
    std::string const svg = R"svg(
        <svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
            <path fill="#2244aa" d="M 10 10 L 90 10 L 90 90 L 10 90 Z M 35 35 L 35 65 L 65 65 L 65 35 Z"/>
        </svg>
    )svg";
    SvgOptions opt;
    opt.dimension = 100;
    auto res = buildSvgPlan(svg, opt);
    if (!res) return check(false, "layered hole path build failed");
    auto frame = renderPlanFrame(res.plan, 0, 1);
    auto alphaAt = [&](int x, int y) {
        return frame[(static_cast<std::size_t>(y) * res.plan.width + x) * 4 + 3];
    };
    bool ok = alphaAt(50, 50) == 0 && alphaAt(20, 50) == 255 && alphaAt(80, 50) == 255;
    return check(ok, "layered path with transparent hole renders center empty and border solid");
}

bool testCurvedPathNoExtremeSkewAndValidSerialization() {
    std::string const svg = R"svg(
        <svg viewBox="0 0 400 400" xmlns="http://www.w3.org/2000/svg">
            <rect x="0" y="0" width="400" height="400" fill="#ffffff"/>
            <path fill="#302050" d="M 50 200 C 50 60 350 60 350 200 C 350 340 50 340 50 200 Z"/>
            <path fill="#f0c040" d="M 200 100 L 230 175 L 310 180 L 245 225 L 265 305 L 200 260 L 135 305 L 155 225 L 90 180 L 170 175 Z"/>
        </svg>
    )svg";
    SvgOptions opt;
    opt.dimension = 200;
    auto res = buildSvgPlan(svg, opt);
    if (!res) return check(false, "curved path + star build failed");
    float maxSkew = 0.f;
    for (auto const& o : res.plan.staticObjects) {
        maxSkew = std::max(maxSkew, std::abs(o.skew));
    }
    // Verify GD serialization produces non-empty parseable entries with no NaN/Inf.
    std::string payload;
    auto shapes = defaultShapes();
    PrimitivePlacement placement;
    placement.imageHeight = res.plan.height;
    for (auto const& o : res.plan.staticObjects) {
        PrimitiveStyle style{shapes[shapeIndex(o.kind)], o.color + 1, nullptr};
        appendSerializedPrimitive(payload, o, style, placement);
    }
    bool noNan = payload.find("nan") == std::string::npos &&
                 payload.find("inf") == std::string::npos;
    // The smooth oval should fit 1 Circle!
    bool ok = res.stats.ellipsesFitted >= 1 && maxSkew <= 74.5f && noNan &&
              res.plan.totalObjects <= 15 && res.plan.frames.size() == 1 &&
              isSvgBytes(reinterpret_cast<std::uint8_t const*>(svg.data()), svg.size()) &&
              isSvgFile("test.SVG");
    return check(ok, "curved oval fits 1 Circle, star stays compact, skew<=74.5, valid GD payload & frame");
}

bool testModeUsesSvgConversionPolicy() {
    bool ok =
        !modeUsesSvgConversion(ImportMode::Blocks) &&
        !modeUsesSvgConversion(ImportMode::Blur) &&
        !modeUsesSvgConversion(ImportMode::Circles) &&
        modeUsesSvgConversion(ImportMode::Art) &&
        modeUsesSvgConversion(ImportMode::Paint) &&
        modeUsesSvgConversion(ImportMode::Render) &&
        modeUsesSvgConversion(ImportMode::Free) &&
        modeUsesSvgConversion(ImportMode::Vert) &&
        modeUsesSvgConversion(ImportMode::VertX) &&
        modeUsesSvgConversion(ImportMode::Svg);
    return check(ok, "modeUsesSvgConversion excludes only Blocks(pixel), Blur, Circles");
}

bool testAnimatedSvgPlanSharedStaticPaletteAndTriggers() {
    std::string const f0 = R"svg(
        <svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
            <rect x="0" y="0" width="100" height="100" fill="#ffffff"/>
            <circle cx="25" cy="50" r="15" fill="#e03040"/>
        </svg>
    )svg";
    std::string const f1 = R"svg(
        <svg viewBox="0 0 100 100" xmlns="http://www.w3.org/2000/svg">
            <rect x="0" y="0" width="100" height="100" fill="#ffffff"/>
            <circle cx="75" cy="50" r="15" fill="#2060e0"/>
        </svg>
    )svg";
    std::vector<SvgFrame> frames{
        {80, f0},
        {90, f1},
        {80, f0}, // repeated frame -> should reuse track 0!
    };
    SvgOptions opt;
    opt.dimension = 100;
    opt.maxColors = 4;
    opt.objectBudget = 500;

    auto resLoop = buildAnimatedSvgPlan(frames, opt, true, 6);
    auto resOnce = buildAnimatedSvgPlan(frames, opt, false, 6);
    if (!resLoop || !resOnce) return check(false, "animated SVG plan build failed");

    auto const& p = resLoop.plan;
    // Expected triggers for loop=true with 3 frames (0: track0, 1: track1, 2: track0):
    // init off: track1 (!vis[0]) -> 1
    // init spawn -> 1
    // frame 0 (prev=2): track0 vis(0)==vis(2)=1, track1 vis(0)==vis(2)=0 -> 0 alphas + 1 spawn
    // frame 1 (prev=0): track0 off, track1 on -> 2 alphas + 1 spawn
    // frame 2 (prev=1): track0 on, track1 off -> 2 alphas + 1 spawn
    // Total triggers = 1 + 1 + 1 + 3 + 3 = 9!
    // Expected triggers for loop=false:
    // init off: 1, init spawn: 1
    // frame 1 (prev=0): 2 alphas + 1 spawn
    // frame 2 (prev=1): 2 alphas + 0 spawn
    // Total triggers = 1 + 1 + 3 + 2 = 7!
    auto px0 = renderPlanFrame(p, 0, 1, false);
    auto px1 = renderPlanFrame(p, 1, 1, false);
    auto atR = [&](std::vector<std::uint8_t> const& buf, int x, int y) {
        return buf[(static_cast<std::size_t>(y) * p.width + x) * 4];
    };
    auto atB = [&](std::vector<std::uint8_t> const& buf, int x, int y) {
        return buf[(static_cast<std::size_t>(y) * p.width + x) * 4 + 2];
    };

    bool ok =
        p.frames.size() == 3 &&
        p.sourceFrames == 6 &&
        p.sampledFrames == 3 &&
        p.palette.size() <= 4 &&
        p.staticObjects.size() == 1 && // shared white backdrop extracted!
        p.tracks.size() == 2 &&        // frame 0 and frame 2 share 1 track!
        p.visualObjects == 3 &&        // 1 static backdrop + 2 circles
        p.triggerObjects == 9 &&
        p.totalObjects == 12 &&
        resOnce.plan.triggerObjects == 7 &&
        resOnce.plan.totalObjects == 10 &&
        atR(px0, 25, 50) > 180 && atB(px0, 25, 50) < 100 && // red circle on left in frame 0
        atB(px1, 75, 50) > 180 && atR(px1, 75, 50) < 100;   // blue circle on right in frame 1
    return check(ok, "animated SVG plan shares static backdrop, deduplicates frames, unifies palette, exact triggers");
}

} // namespace

int main() {
    bool ok = true;
    ok &= testDirectRectAndCircle();
    ok &= testOccludedShapePruned();
    ok &= testPlanarPartitionHoleEliminationAndSameColorMerge();
    ok &= testLayeredPathWithHoleTriangulation();
    ok &= testCurvedPathNoExtremeSkewAndValidSerialization();
    ok &= testModeUsesSvgConversionPolicy();
    ok &= testAnimatedSvgPlanSharedStaticPaletteAndTriggers();
    return ok ? 0 : 1;
}
