#include "GifPlanOptimizer.hpp"

#include "ColorSpace.hpp"
#include "GifArtVectorizer.hpp"
#include "GifShapeRaster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace paimon::gifimport {
namespace {

Color sourceColor(std::span<std::uint8_t const> rgba, std::size_t cell) {
    return {rgba[cell * 4], rgba[cell * 4 + 1], rgba[cell * 4 + 2]};
}

int sourceAlpha(std::span<std::uint8_t const> rgba, std::size_t cell) {
    return rgba[cell * 4 + 3];
}

bool sameColor(Color first, Color second) {
    return first.r == second.r && first.g == second.g && first.b == second.b;
}

float maxLabDistance() {
    static float const distance = oklabDistance({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f});
    return distance;
}

// keep every palette entry perceptually separated from the others. The old
// color is valid by construction, so a short search between both is enough.
Color separatedColor(
    Color candidate,
    Color previous,
    std::size_t index,
    std::vector<OkLab> const& labs
) {
    if (sameColor(candidate, previous)) return candidate;
    auto valid = [&](Color color) {
        OkLab const lab = rgbToOkLab(color);
        for (std::size_t other = 0; other < labs.size(); ++other) {
            if (other == index) continue;
            if (oklabDistance(lab, labs[other]) < kPaletteMinDistance) return false;
        }
        return true;
    };
    if (valid(candidate)) return candidate;
    float low = 0.f, high = 1.f;
    for (int step = 0; step < 12; ++step) {
        float const ratio = (low + high) * 0.5f;
        Color const trial{
            static_cast<std::uint8_t>(std::lround(
                previous.r + ratio * (static_cast<float>(candidate.r) - previous.r))),
            static_cast<std::uint8_t>(std::lround(
                previous.g + ratio * (static_cast<float>(candidate.g) - previous.g))),
            static_cast<std::uint8_t>(std::lround(
                previous.b + ratio * (static_cast<float>(candidate.b) - previous.b)))};
        if (valid(trial)) low = ratio;
        else high = ratio;
    }
    return {
        static_cast<std::uint8_t>(std::lround(
            previous.r + low * (static_cast<float>(candidate.r) - previous.r))),
        static_cast<std::uint8_t>(std::lround(
            previous.g + low * (static_cast<float>(candidate.g) - previous.g))),
        static_cast<std::uint8_t>(std::lround(
            previous.b + low * (static_cast<float>(candidate.b) - previous.b)))};
}

int dominantIndex(
    std::span<std::int32_t const> indices,
    int planWidth,
    int scale,
    int x,
    int y
) {
    int best = -1;
    int bestCount = 0;
    int const stride = planWidth * scale;
    for (int sy = 0; sy < scale; ++sy) for (int sx = 0; sx < scale; ++sx) {
        int const index = indices[
            static_cast<std::size_t>(y * scale + sy) * stride + x * scale + sx];
        if (index < 0) continue;
        int count = 0;
        for (int oy = 0; oy < scale; ++oy) for (int ox = 0; ox < scale; ++ox) {
            if (indices[static_cast<std::size_t>(y * scale + oy) * stride + x * scale + ox] == index)
                ++count;
        }
        if (count > bestCount || (count == bestCount && index < best)) {
            best = index;
            bestCount = count;
        }
    }
    return best;
}

double paletteError(
    std::vector<Color> const& palette,
    std::span<std::int32_t const> indices,
    std::span<std::uint8_t const> source,
    int width,
    int height,
    int scale,
    int alphaThreshold
) {
    std::vector<OkLab> labs;
    labs.reserve(palette.size());
    for (auto const& color : palette) labs.push_back(rgbToOkLab(color));
    double total = 0.0;
    std::size_t cells = 0;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        std::size_t const cell = static_cast<std::size_t>(y) * width + x;
        int const alpha = sourceAlpha(source, cell);
        int const index = dominantIndex(indices, width, scale, x, y);
        ++cells;
        if (alpha < alphaThreshold) {
            if (index >= 0) total += maxLabDistance();
            continue;
        }
        if (index < 0 || index >= static_cast<int>(palette.size())) {
            total += maxLabDistance();
            continue;
        }
        total += oklabDistance(
            labs[static_cast<std::size_t>(index)],
            rgbToOkLab(sourceColor(source, cell)));
    }
    return cells > 0 ? total / static_cast<double>(cells) : 0.0;
}

} // namespace

PaletteRefineResult refinePaletteAgainstRender(
    ImportPlan& plan,
    std::span<std::uint8_t const> sourceRgba,
    int alphaThreshold
) {
    PaletteRefineResult result;
    if (plan.frames.size() != 1 || plan.palette.empty()) return result;
    std::size_t const cells = static_cast<std::size_t>(plan.width) * plan.height;
    if (plan.frames.front().cells.size() != cells || sourceRgba.size() < cells * 4) return result;

    constexpr int scale = 2;
    auto const indices = renderPlanFrameIndices(plan, 0, scale);
    if (indices.size() !=
        static_cast<std::size_t>(plan.width) * scale * plan.height * scale) {
        return result;
    }

    std::vector<std::array<double, 4>> sums(plan.palette.size());
    for (int y = 0; y < plan.height; ++y) for (int x = 0; x < plan.width; ++x) {
        int const index = dominantIndex(indices, plan.width, scale, x, y);
        if (index < 0 || index >= static_cast<int>(plan.palette.size())) continue;
        std::size_t const cell = static_cast<std::size_t>(y) * plan.width + x;
        int const alpha = sourceAlpha(sourceRgba, cell);
        if (alpha < alphaThreshold) continue;
        double const weight = static_cast<double>(alpha) / 255.0;
        auto& sum = sums[static_cast<std::size_t>(index)];
        OkLab const target = rgbToOkLab(sourceColor(sourceRgba, cell));
        sum[0] += target.L * weight;
        sum[1] += target.a * weight;
        sum[2] += target.b * weight;
        sum[3] += weight;
    }

    result.errorBefore = paletteError(
        plan.palette, indices, sourceRgba, plan.width, plan.height, scale, alphaThreshold);
    auto refined = plan.palette;
    std::vector<OkLab> labs;
    labs.reserve(refined.size());
    for (auto const& color : refined) labs.push_back(rgbToOkLab(color));
    for (std::size_t index = 0; index < sums.size(); ++index) {
        auto const& sum = sums[index];
        if (sum[3] <= 0.0) continue;
        Color const candidate = oklabToRgb({
            static_cast<float>(sum[0] / sum[3]),
            static_cast<float>(sum[1] / sum[3]),
            static_cast<float>(sum[2] / sum[3])});
        refined[index] = separatedColor(candidate, refined[index], index, labs);
        labs[index] = rgbToOkLab(refined[index]);
    }
    result.errorAfter = paletteError(
        refined, indices, sourceRgba, plan.width, plan.height, scale, alphaThreshold);
    if (result.errorAfter + 1e-6 < result.errorBefore) {
        plan.palette = std::move(refined);
        result.changed = true;
    } else {
        result.errorAfter = result.errorBefore;
    }
    return result;
}

DetailStats restorePlanDetail(
    ImportPlan& plan,
    std::size_t objectBudget,
    std::span<std::uint8_t const> sourceRgba,
    int alphaThreshold,
    int detailPercent
) {
    DetailStats stats;
    if (plan.frames.size() != 1 || plan.palette.empty()) return stats;
    std::size_t const cells = static_cast<std::size_t>(plan.width) * plan.height;
    if (plan.frames.front().cells.size() != cells || sourceRgba.size() < cells * 4) return stats;
    std::size_t const current = plan.staticObjects.size() + plan.triggerObjects;
    if (current >= objectBudget) return stats;

    constexpr int scale = 2;
    auto const indices = renderPlanFrameIndices(plan, 0, scale);
    if (indices.size() !=
        static_cast<std::size_t>(plan.width) * scale * plan.height * scale) {
        return stats;
    }
    std::vector<OkLab> labs;
    labs.reserve(plan.palette.size());
    for (auto const& color : plan.palette) labs.push_back(rgbToOkLab(color));

    struct Candidate {
        double gain = 0.0;
        int cell = 0;
        int color = 0;
    };
    std::vector<Candidate> candidates;
    for (int y = 0; y < plan.height; ++y) for (int x = 0; x < plan.width; ++x) {
        std::size_t const cell = static_cast<std::size_t>(y) * plan.width + x;
        if (sourceAlpha(sourceRgba, cell) < alphaThreshold) continue;
        int const expected = plan.frames.front().cells[cell];
        if (expected < 0 || expected >= static_cast<int>(plan.palette.size())) continue;
        int const visible = dominantIndex(indices, plan.width, scale, x, y);
        if (visible == expected) continue;
        OkLab const target = rgbToOkLab(sourceColor(sourceRgba, cell));
        double const currentError = visible < 0
            ? static_cast<double>(maxLabDistance())
            : static_cast<double>(oklabDistance(labs[static_cast<std::size_t>(visible)], target));
        double const targetError =
            oklabDistance(labs[static_cast<std::size_t>(expected)], target);
        if (targetError + 1e-4 >= currentError) continue;
        candidates.push_back({currentError - targetError,
            static_cast<int>(cell), expected});
    }
    if (candidates.empty()) return stats;
    std::sort(candidates.begin(), candidates.end(), [](Candidate const& first, Candidate const& second) {
        if (std::abs(first.gain - second.gain) > 1e-9) return first.gain > second.gain;
        return first.cell < second.cell;
    });

    std::size_t const headroom = objectBudget - current;
    std::size_t const requested = std::max<std::size_t>(
        32, current * static_cast<std::size_t>(std::max(1, detailPercent)) / 100);
    std::size_t const cap = std::min(headroom, requested);
    if (cap == 0) return stats;

    std::int16_t layer = -998;
    for (auto const& object : plan.staticObjects) layer = std::max(layer, object.layer);
    layer = static_cast<std::int16_t>(std::min<int>(layer + 1, 999));

    // group nearby patches of the same color into rectangles. A run of cells
    // costs one object instead of one per cell, so the same budget buys more
    // detail and leaves fewer pieces to merge later.
    std::size_t const cellLimit = std::min(candidates.size(), cap * 16);
    std::map<int, std::vector<int>> byColor;
    std::vector<double> cellGain(cells, 0.0);
    for (std::size_t index = 0; index < cellLimit; ++index) {
        auto const& candidate = candidates[index];
        byColor[candidate.color].push_back(candidate.cell);
        cellGain[static_cast<std::size_t>(candidate.cell)] = candidate.gain;
    }
    struct PackedPatch {
        double gain = 0.0;
        Primitive object;
    };
    std::vector<PackedPatch> rectangles;
    for (auto const& [color, positions] : byColor) {
        auto blocks = packBlocks(positions, plan.width, plan.height, color);
        for (auto& block : blocks) {
            block.layer = layer;
            int const left = static_cast<int>(std::lround(block.x - block.width * 0.5f));
            int const top = static_cast<int>(std::lround(block.y - block.height * 0.5f));
            int const right = static_cast<int>(std::lround(block.x + block.width * 0.5f));
            int const bottom = static_cast<int>(std::lround(block.y + block.height * 0.5f));
            double gain = 0.0;
            for (int y = std::max(0, top); y < std::min(plan.height, bottom); ++y)
                for (int x = std::max(0, left); x < std::min(plan.width, right); ++x)
                    gain += cellGain[static_cast<std::size_t>(y) * plan.width + x];
            rectangles.push_back({gain, std::move(block)});
        }
    }
    std::sort(rectangles.begin(), rectangles.end(),
        [](PackedPatch const& first, PackedPatch const& second) {
            if (std::abs(first.gain - second.gain) > 1e-9)
                return first.gain > second.gain;
            if (first.object.color != second.object.color)
                return first.object.color < second.object.color;
            return first.object.y < second.object.y ||
                (first.object.y == second.object.y && first.object.x < second.object.x);
        });
    for (auto const& patch : rectangles) {
        if (stats.added >= cap) break;
        plan.staticObjects.push_back(patch.object);
        ++stats.added;
    }
    plan.restoredObjects += stats.added;
    return stats;
}

TrimStats trimPlanToBudget(
    ImportPlan& plan,
    std::size_t objectBudget,
    std::span<std::uint8_t const> sourceRgba,
    int alphaThreshold
) {
    TrimStats stats;
    if (plan.frames.size() != 1 || plan.palette.empty()) return stats;
    if (!plan.tracks.empty() || !plan.motionTracks.empty()) return stats;
    std::size_t const cells = static_cast<std::size_t>(plan.width) * plan.height;
    if (plan.frames.front().cells.size() != cells || sourceRgba.size() < cells * 4) return stats;

    std::size_t const target = objectBudget > plan.triggerObjects
        ? objectBudget - plan.triggerObjects : 0;
    if (target == 0 || plan.staticObjects.size() <= target) {
        stats.fitted = plan.staticObjects.size() <= target;
        return stats;
    }

    int const width = plan.width;
    int const height = plan.height;
    auto const& objects = plan.staticObjects;
    std::size_t const count = objects.size();
    std::vector<OkLab> labs;
    labs.reserve(plan.palette.size());
    for (auto const& color : plan.palette) labs.push_back(rgbToOkLab(color));

    std::vector<OkLab> cellLabs(cells);
    std::vector<std::uint8_t> cellVisible(cells, 0);
    std::vector<std::uint8_t> interior(cells, 0);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        std::size_t const cell = static_cast<std::size_t>(y) * width + x;
        int const alpha = sourceAlpha(sourceRgba, cell);
        if (alpha < alphaThreshold) continue;
        cellVisible[cell] = 1;
        cellLabs[cell] = rgbToOkLab(sourceColor(sourceRgba, cell));
    }
    for (int y = 1; y + 1 < height; ++y) for (int x = 1; x + 1 < width; ++x) {
        std::size_t const cell = static_cast<std::size_t>(y) * width + x;
        if (sourceAlpha(sourceRgba, cell) < 250) continue;
        int const color = plan.frames.front().cells[cell];
        if (color < 0) continue;
        bool same = true;
        for (int dy = -1; dy <= 1 && same; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (plan.frames.front().cells[
                        static_cast<std::size_t>(y + dy) * width + x + dx] != color) {
                    same = false;
                    break;
                }
        if (same) interior[cell] = 1;
    }

    // top two owners on a 2x2 sample grid: the gain of an object is the error
    // that appears when the next color shows through.
    constexpr int attributionScale = 2;
    int const sampleWidth = width * attributionScale;
    int const sampleHeight = height * attributionScale;
    std::vector<std::int32_t> top(
        static_cast<std::size_t>(sampleWidth) * sampleHeight, -1);
    std::vector<std::int32_t> below(top.size(), -1);
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t first, std::size_t second) {
        return objects[first].layer < objects[second].layer;
    });
    for (std::size_t index : order) {
        auto const shape = xformOf(objects[index], plan.stamps);
        forEachSample(shape, width, height, attributionScale, [&](int x, int y) {
            std::size_t const sample = static_cast<std::size_t>(y) * sampleWidth + x;
            below[sample] = top[sample];
            top[sample] = static_cast<std::int32_t>(index);
            return false;
        });
    }

    std::vector<double> gain(count, 0.0);
    std::vector<std::uint32_t> visible(count, 0);
    for (std::int32_t sample = 0; sample < static_cast<std::int32_t>(top.size()); ++sample) {
        int const owner = top[static_cast<std::size_t>(sample)];
        if (owner < 0) continue;
        ++visible[static_cast<std::size_t>(owner)];
        int const sx = sample % sampleWidth;
        int const sy = sample / sampleWidth;
        std::size_t const cell = static_cast<std::size_t>(sy / attributionScale) * width +
            sx / attributionScale;
        if (!cellVisible[cell]) continue;
        int const ownerColor = objects[static_cast<std::size_t>(owner)].color;
        if (ownerColor >= static_cast<int>(labs.size())) continue;
        double const currentError = oklabDistance(labs[static_cast<std::size_t>(ownerColor)], cellLabs[cell]);
        int const belowOwner = below[static_cast<std::size_t>(sample)];
        double afterError = maxLabDistance();
        if (belowOwner >= 0) {
            int const belowColor = objects[static_cast<std::size_t>(belowOwner)].color;
            if (belowColor == ownerColor) continue;
            if (belowColor < static_cast<int>(labs.size())) {
                afterError = oklabDistance(
                    labs[static_cast<std::size_t>(belowColor)], cellLabs[cell]);
            }
        }
        if (afterError > currentError) {
            gain[static_cast<std::size_t>(owner)] += afterError - currentError;
        }
    }

    std::vector<std::size_t> candidates(count);
    std::iota(candidates.begin(), candidates.end(), 0);
    std::sort(candidates.begin(), candidates.end(), [&](std::size_t first, std::size_t second) {
        if (std::abs(gain[first] - gain[second]) > 1e-9) return gain[first] < gain[second];
        if (visible[first] != visible[second]) return visible[first] < visible[second];
        return first > second;
    });

    std::size_t const excess = count - target;
    std::vector<std::uint8_t> keep(count, 1);
    for (std::size_t index = 0; index < std::min(excess, count); ++index) {
        keep[candidates[index]] = 0;
    }

    constexpr int validationScale = 4;
    auto renderIndices = [&](std::vector<Primitive> const& list) {
        ImportPlan temporary = plan;
        temporary.staticObjects = list;
        temporary.tracks.clear();
        temporary.motionTracks.clear();
        return renderPlanFrameIndices(temporary, 0, validationScale);
    };
    auto const baselineIndices = renderIndices(objects);
    if (baselineIndices.size() !=
        cells * validationScale * validationScale) {
        return stats;
    }
    std::vector<std::uint8_t> beforeCovered(cells, 0);
    std::vector<std::uint8_t> stable(cells, 0);
    std::vector<std::int32_t> expected(cells, -1);
    int const sampleStride = width * validationScale;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        std::size_t const cell = static_cast<std::size_t>(y) * width + x;
        bool covered = false;
        int top = -1;
        bool same = true;
        for (int sy = 0; sy < validationScale; ++sy)
            for (int sx = 0; sx < validationScale; ++sx) {
                int const index = baselineIndices[
                    static_cast<std::size_t>(y * validationScale + sy) * sampleStride +
                    x * validationScale + sx];
                if (index >= 0) covered = true;
                if (top < 0) top = index;
                else if (index != top) same = false;
            }
        beforeCovered[cell] = covered ? 1 : 0;
        if (interior[cell] && covered && same && top >= 0) {
            stable[cell] = 1;
            expected[cell] = top;
        }
    }

    std::vector<Primitive> kept;
    kept.reserve(target);
    for (std::size_t index = 0; index < count; ++index)
        if (keep[index]) kept.push_back(objects[index]);
    auto afterIndices = renderIndices(kept);

    auto missingCells = [&] {
        std::vector<std::uint8_t> missing(cells, 0);
        bool any = false;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            std::size_t const cell = static_cast<std::size_t>(y) * width + x;
            if (!beforeCovered[cell]) continue;
            bool covered = false;
            bool wrong = false;
            for (int sy = 0; sy < validationScale; ++sy)
                for (int sx = 0; sx < validationScale; ++sx) {
                    int const index = afterIndices[
                        static_cast<std::size_t>(y * validationScale + sy) * sampleStride +
                        x * validationScale + sx];
                    if (index >= 0) covered = true;
                    if (stable[cell] && index != expected[cell]) wrong = true;
                }
            if (!covered || wrong) {
                missing[cell] = 1;
                any = true;
            }
        }
        return any ? missing : std::vector<std::uint8_t>{};
    };

    auto missing = missingCells();
    if (!missing.empty()) {
        // restore the highest-layer removed pieces first: those are the ones
        // that put the expected color back on top. Validate in small batches;
        // a single full index render per restored object would be too slow.
        std::vector<std::size_t> restoreOrder(count);
        std::iota(restoreOrder.begin(), restoreOrder.end(), 0);
        std::stable_sort(restoreOrder.begin(), restoreOrder.end(),
            [&](std::size_t first, std::size_t second) {
                return objects[first].layer > objects[second].layer;
            });
        int restored = 0;
        int pending = 0;
        for (std::size_t index : restoreOrder) {
            if (keep[index]) continue;
            auto const shape = xformOf(objects[index], plan.stamps);
            bool helps = false;
            forEachSample(shape, width, height, validationScale, [&](int x, int y) {
                std::size_t const cell =
                    static_cast<std::size_t>(y / validationScale) * width + x / validationScale;
                if (!missing[cell]) return false;
                if (!stable[cell] || objects[index].color == expected[cell]) {
                    helps = true;
                    return true;
                }
                return false;
            });
            if (!helps) continue;
            keep[index] = 1;
            ++restored;
            if (++pending >= 8) {
                pending = 0;
                kept.clear();
                for (std::size_t slot = 0; slot < count; ++slot)
                    if (keep[slot]) kept.push_back(objects[slot]);
                afterIndices = renderIndices(kept);
                missing = missingCells();
                if (missing.empty()) break;
            }
            if (restored >= 96) break;
        }
        if (!missing.empty()) {
            kept.clear();
            for (std::size_t slot = 0; slot < count; ++slot)
                if (keep[slot]) kept.push_back(objects[slot]);
            afterIndices = renderIndices(kept);
            missing = missingCells();
        }
        if (!missing.empty()) return stats;
    }
    kept.clear();
    kept.reserve(target);
    for (std::size_t index = 0; index < count; ++index)
        if (keep[index]) kept.push_back(objects[index]);
    if (kept.size() > target) return stats;

    std::size_t const removed = count - kept.size();
    plan.staticObjects = std::move(kept);
    plan.trimmedObjects += removed;
    stats.fitted = true;
    stats.removed = removed;
    return stats;
}

float absolutePlanSimilarity(
    ImportPlan const& plan,
    std::span<std::uint8_t const> referenceRgba,
    int referenceWidth,
    int referenceHeight
) {
    if (plan.frames.empty() || plan.width <= 0 || plan.height <= 0 ||
        referenceWidth <= 0 || referenceHeight <= 0 ||
        referenceRgba.size() < static_cast<std::size_t>(referenceWidth) * referenceHeight * 4) {
        return 100.f;
    }
    auto const rendered = renderPlanFrame(plan, 0, 1);
    std::size_t const expected = static_cast<std::size_t>(plan.width) * plan.height * 4;
    if (rendered.size() < expected) return 100.f;

    double const norm = 255.0 * std::sqrt(3.0);
    double score = 0.0;
    std::size_t compared = 0;
    for (int y = 0; y < referenceHeight; ++y) for (int x = 0; x < referenceWidth; ++x) {
        int const sourceX = std::min(plan.width - 1,
            static_cast<int>((x + 0.5) * plan.width / referenceWidth));
        int const sourceY = std::min(plan.height - 1,
            static_cast<int>((y + 0.5) * plan.height / referenceHeight));
        std::size_t const renderedIndex =
            (static_cast<std::size_t>(sourceY) * plan.width + sourceX) * 4;
        std::size_t const referenceIndex =
            (static_cast<std::size_t>(y) * referenceWidth + x) * 4;
        bool const renderedVisible = rendered[renderedIndex + 3] != 0;
        bool const referenceVisible = referenceRgba[referenceIndex + 3] != 0;
        if (!renderedVisible && !referenceVisible) continue;
        ++compared;
        if (!renderedVisible || !referenceVisible) continue;
        double const dr = static_cast<double>(rendered[renderedIndex]) - referenceRgba[referenceIndex];
        double const dg = static_cast<double>(rendered[renderedIndex + 1]) - referenceRgba[referenceIndex + 1];
        double const db = static_cast<double>(rendered[renderedIndex + 2]) - referenceRgba[referenceIndex + 2];
        score += 1.0 - std::sqrt(dr * dr + dg * dg + db * db) / norm;
    }
    return compared > 0
        ? static_cast<float>(100.0 * score / static_cast<double>(compared))
        : 100.f;
}

} // namespace paimon::gifimport
