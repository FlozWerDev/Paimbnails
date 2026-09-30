#include "GifGlowPass.hpp"

#include <algorithm>
#include <map>
#include <vector>

namespace paimon::gifimport {

namespace {

constexpr std::size_t kMaxGlowColors = 4;

float luminance(Color const& color) {
    return 0.299f * color.r + 0.587f * color.g + 0.114f * color.b;
}

float saturation(Color const& color) {
    int const high = std::max({color.r, color.g, color.b});
    int const low = std::min({color.r, color.g, color.b});
    if (high == 0) return 0.f;
    return static_cast<float>(high - low) / high;
}

// only colors reading as light glow: brights and saturateds. glow under a
// mid gray only dirties the drawing.
std::vector<std::uint16_t> glowingColors(std::vector<Color> const& palette) {
    std::vector<std::uint16_t> colors;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        float const luma = luminance(palette[i]);
        float const sat = saturation(palette[i]);
        if (luma < 110.f) continue;
        if (sat < 0.25f && luma < 200.f) continue;
        colors.push_back(static_cast<std::uint16_t>(i));
    }
    std::sort(colors.begin(), colors.end(), [&](std::uint16_t left, std::uint16_t right) {
        auto score = [&](std::uint16_t index) {
            return luminance(palette[index]) * (0.5f + saturation(palette[index]));
        };
        return score(left) > score(right);
    });
    if (colors.size() > kMaxGlowColors) colors.resize(kMaxGlowColors);
    return colors;
}

struct GlowTarget {
    std::vector<Primitive>* owner;
    std::size_t index;
    float area;
};

void collect(std::vector<Primitive>& objects, std::vector<int> const& map,
             std::vector<GlowTarget>& candidates) {
    for (std::size_t i = 0; i < objects.size(); ++i) {
        auto const& object = objects[i];
        if (object.color >= map.size() || map[object.color] < 0) continue;
        float const area = object.width * object.height;
        if (area < 1.5f) continue;
        candidates.push_back({&objects, i, area});
    }
}

Primitive halo(Primitive const& source, int color, float ring) {
    Primitive glow = source;
    glow.width = source.width + ring * 2.f;
    glow.height = source.height + ring * 2.f;
    glow.color = static_cast<std::uint16_t>(color);
    glow.layer = -999;
    if (glow.kind == PrimitiveKind::Block || glow.kind == PrimitiveKind::Stroke) {
        glow.kind = PrimitiveKind::Glow;
    }
    return glow;
}

} // namespace

void applyGlow(ImportPlan& plan, GlowMode mode, std::size_t objectBudget) {
    if (mode == GlowMode::Off || plan.palette.empty()) return;
    if (plan.totalObjects >= objectBudget) return;

    auto const colors = glowingColors(plan.palette);
    if (colors.empty()) return;

    std::vector<int> map(plan.palette.size(), -1);
    plan.glowPaletteStart = plan.palette.size();
    plan.glowOpacity = mode == GlowMode::Strong ? 0.55f : 0.35f;
    for (auto color : colors) {
        map[color] = static_cast<int>(plan.palette.size());
        auto const copy = plan.palette[color];
        plan.palette.push_back(copy);
    }

    std::vector<GlowTarget> candidates;
    collect(plan.staticObjects, map, candidates);
    for (auto& track : plan.tracks) collect(track.objects, map, candidates);
    for (auto& track : plan.motionTracks) collect(track.objects, map, candidates);
    std::sort(candidates.begin(), candidates.end(),
              [](GlowTarget const& left, GlowTarget const& right) {
                  return left.area > right.area;
              });

    std::size_t const room = std::min(objectBudget / 5, objectBudget - plan.totalObjects);
    if (candidates.size() > room) candidates.resize(room);
    if (candidates.empty()) {
        plan.palette.resize(plan.glowPaletteStart);
        plan.glowPaletteStart = static_cast<std::size_t>(-1);
        return;
    }

    float const ring = mode == GlowMode::Strong ? 1.1f : 0.55f;
    std::map<std::vector<Primitive>*, std::vector<Primitive>> halos;
    for (auto const& candidate : candidates) {
        auto const& source = (*candidate.owner)[candidate.index];
        halos[candidate.owner].push_back(halo(source, map[source.color], ring));
    }
    // front of vector is back of drawing: in blocks mode payload order alone
    // decides who covers whom.
    for (auto& [owner, glow] : halos) {
        owner->insert(owner->begin(), glow.begin(), glow.end());
    }

    plan.glowObjects = candidates.size();
    plan.visualObjects += candidates.size();
    plan.totalObjects += candidates.size();
}

} // namespace paimon::gifimport
