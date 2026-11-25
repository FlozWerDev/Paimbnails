#include "GifSurfaceOptimizer.hpp"

#include "GifArtVectorizer.hpp"
#include "GifCompute.hpp"
#include "GifPaintVectorizer.hpp"
#include "GifShapeRaster.hpp"
#include "GifSurfaceVectorizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace paimon::gifimport {
namespace surfaceopt {

// limits refer to source rgb error, not agreement with a quantized staircase.
// a smoother band may deviate a few percent from the tightest fit: measured
// against the continuous source that reads as a cleaner edge, not as damage.
constexpr double kMaximumErrorRatio = 1.04;
constexpr double kPixelErrorSlack = 0.12;
// two independently curved sides can need a block and triangle per interval,
// plus interior repairs. keep that option bounded without forcing pixel steps
// simply because their rectangles were cheaper.
constexpr std::size_t kMaximumContourCostRatio = 3;
constexpr int kCoverageScale = 4;
constexpr int kCoverageSamples = kCoverageScale * kCoverageScale;

struct Coverage {
    std::vector<std::uint8_t> counts;
    std::vector<std::uint16_t> centers;
    std::vector<std::uint8_t> opaqueCounts;
};

bool isOpaqueSample(ShapeXform const& form, float x, float y) {
    if (form.kind != PrimitiveKind::Circle && form.kind != PrimitiveKind::Triangle &&
        form.kind != PrimitiveKind::WideTriangle) return true;
    float const dx = x - form.x, dy = y - form.y;
    float const rotatedY = -dx * form.sine + dy * form.cosine;
    float const localX = dx * form.cosine + dy * form.sine + form.skewTangent * rotatedY;
    float const localY = rotatedY / form.skewCosine;
    if (form.kind == PrimitiveKind::Circle) {
        float const nx = localX / (form.width * 0.5f), ny = localY / (form.height * 0.5f);
        // d_circle_02 has a filtered fringe inside its mathematical radius.
        // it cannot seal an opaque interior unless another solid covers it.
        constexpr float opaqueRadius = 0.99f;
        return nx * nx + ny * ny < opaqueRadius * opaqueRadius;
    }
    // the native 693/694 texture has an antialiased hypotenuse. a point on
    // that mathematical edge is covered, but is not solid ink (alpha 95/255
    // on the uhd 120-pixel diagonal). protect interiors inside that fringe.
    constexpr float opaqueInset = 1.5f / 120.f;
    return localX / form.width + localY / form.height < -opaqueInset;
}

class ColorRegion {
public:
    ColorRegion(SurfaceImage const& image, std::size_t color, std::vector<int> const& positions)
        : m_image(image), m_color(color), m_permitted(static_cast<std::size_t>(image.width) * image.height, 0) {
        for (int cell : positions) m_permitted[cell] = 1;
        std::vector<std::uint8_t> evaluated(m_permitted.size(), 0);
        auto const& later = image.coveredLater[color];
        for (int cell : positions) {
            int const cx = cell % image.width, cy = cell / image.width;
            // include the edge fringe as well as the fill, but never let
            // unrelated parts of a larger canvas fund a damaged detail.
            for (int y = std::max(0, cy - 2); y <= std::min(image.height - 1, cy + 2); ++y)
                for (int x = std::max(0, cx - 2); x <= std::min(image.width - 1, cx + 2); ++x) {
                    auto const neighbor = static_cast<std::size_t>(y) * image.width + x;
                    if (later.empty() || !later[neighbor]) evaluated[neighbor] = 1;
                }
        }
        for (std::size_t cell = 0; cell < evaluated.size(); ++cell)
            if (evaluated[cell]) m_evaluationCells.push_back(static_cast<int>(cell));
        for (int cell : positions) {
            int const x = cell % image.width, y = cell / image.width;
            if (x == 0 || y == 0 || x + 1 == image.width || y + 1 == image.height || image.rgba[cell * 4 + 3] < 250) continue;
            bool interior = true;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                interior &= m_permitted[cell + dy * image.width + dx] != 0;
            if (interior) m_interior.push_back(cell);
        }
        if (later.size() == m_permitted.size())
            for (std::size_t cell = 0; cell < later.size(); ++cell) m_permitted[cell] |= later[cell];
        for (int y = 2; y + 2 < image.height; ++y) for (int x = 2; x + 2 < image.width; ++x) {
            int const cell = y * image.width + x;
            if (m_permitted[cell]) continue;
            bool interior = true;
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                interior &= m_permitted[cell + dy * image.width + dx] == 0;
            if (interior) m_foreignInterior.push_back(cell);
        }
    }

    bool repairInterior(std::vector<Primitive>& shapes, Coverage const& coverage) const {
        std::vector<int> missing;
        for (int cell : m_interior) if (coverage.opaqueCounts[cell] != kCoverageSamples) missing.push_back(cell);
        if (missing.empty()) return false;
        auto repairs = packBlocks(missing, m_image.width, m_image.height, static_cast<int>(m_color), m_permitted);
        for (auto& repair : repairs) repair.layer = static_cast<std::int16_t>(m_image.ranks[m_color] * kPaintSublayers);
        shapes.insert(shapes.end(), repairs.begin(), repairs.end());
        mergePaintSolids(shapes);
        return true;
    }

    bool invadesProtectedRegions(Coverage const& coverage) const {
        return std::any_of(m_foreignInterior.begin(), m_foreignInterior.end(), [&](int cell) {
            return coverage.counts[cell] != 0;
        });
    }

    std::span<int const> evaluationCells() const { return m_evaluationCells; }

private:
    SurfaceImage const& m_image;
    std::size_t m_color;
    std::vector<std::uint8_t> m_permitted;
    std::vector<int> m_interior;
    std::vector<int> m_foreignInterior;
    std::vector<int> m_evaluationCells;
};

class Composite {
public:
    explicit Composite(SurfaceImage const& image) : m_image(image),
        m_cells(static_cast<std::size_t>(image.width) * image.height), m_below(m_cells) {
        for (std::size_t cell = 0; cell < m_cells; ++cell)
            m_protectTransparency |= image.rgba[cell * 4 + 3] < 255;
        if (m_protectTransparency) m_centers.resize(m_cells, 0);
    }

    Coverage measure(std::vector<Primitive> const& shapes) const {
        std::vector<std::uint32_t> computed;
        if (computeSurfaceCoverage(xformsOf(shapes), m_image.width, m_image.height,
            m_protectTransparency, computed)) {
            Coverage result{std::vector<std::uint8_t>(m_cells), {}, std::vector<std::uint8_t>(m_cells)};
            if (m_protectTransparency) result.centers.resize(m_cells);
            for (std::size_t cell = 0; cell < m_cells; ++cell) {
                result.counts[cell] = static_cast<std::uint8_t>(computed[cell] & 31);
                result.opaqueCounts[cell] = static_cast<std::uint8_t>((computed[cell] >> 5) & 31);
                if (m_protectTransparency) result.centers[cell] = static_cast<std::uint16_t>(computed[cell] >> 10);
            }
            return result;
        }
        return measureOnCpu(shapes);
    }

    Coverage measureOnCpu(std::vector<Primitive> const& shapes) const {
        std::vector<std::uint8_t> samples(m_cells * kCoverageSamples, 0);
        Coverage result{std::vector<std::uint8_t>(m_cells, 0), {}, std::vector<std::uint8_t>(m_cells, 0)};
        if (m_protectTransparency) result.centers.resize(m_cells, 0);
        for (auto const& shape : shapes) {
            auto const form = xformOf(shape);
            forEachSample(form, m_image.width, m_image.height, kCoverageScale, [&](int x, int y) {
                auto& sample = samples[static_cast<std::size_t>(y) * m_image.width * kCoverageScale + x];
                sample |= isOpaqueSample(form, (x + 0.5f) / kCoverageScale, (y + 0.5f) / kCoverageScale) ? 3 : 1;
                return false;
            });
            if (m_protectTransparency) markCenters(form, result.centers);
        }
        for (int y = 0; y < m_image.height * kCoverageScale; ++y) for (int x = 0; x < m_image.width * kCoverageScale; ++x) {
            auto const cell = static_cast<std::size_t>(y / kCoverageScale) * m_image.width + x / kCoverageScale;
            auto const sample = samples[static_cast<std::size_t>(y) * m_image.width * kCoverageScale + x];
            result.counts[cell] += sample & 1;
            result.opaqueCounts[cell] += (sample >> 1) & 1;
        }
        return result;
    }

    double error(Coverage const& mask, std::size_t color, Coverage const& required,
        std::span<int const> evaluationCells) const {
        auto const ink = m_image.palette[color];
        double total = 0.;
        for (int cell : evaluationCells) {
            auto const target = m_image.rgba.subspan(cell * 4, 4);
            if (m_protectTransparency && target[3] != 0 && ((mask.centers[cell] | m_centers[cell]) &
                required.centers[cell]) != required.centers[cell])
                return std::numeric_limits<double>::infinity();
            float const alpha = static_cast<float>(mask.counts[cell]) / kCoverageSamples;
            float const targetAlpha = target[3] / 255.f;
            double cellError = 0.;
            cellError += std::abs(ink.r * alpha + m_below[cell][0] * (1.f - alpha) - target[0] * targetAlpha);
            cellError += std::abs(ink.g * alpha + m_below[cell][1] * (1.f - alpha) - target[1] * targetAlpha);
            cellError += std::abs(ink.b * alpha + m_below[cell][2] * (1.f - alpha) - target[2] * targetAlpha);
            cellError += 765.f * std::abs(alpha + m_below[cell][3] * (1.f - alpha) - targetAlpha);
            total += cellError;
        }
        return total;
    }

    std::vector<float> estimateStrokeCoverage(std::size_t color, std::span<int const> positions,
        std::span<int const> evaluationCells) const {
        std::vector<float> coverage(m_cells, 0.f);
        for (int cell : positions) coverage[cell] = 1.f;
        auto const ink = m_image.palette[color];
        for (int cell : evaluationCells) {
            auto const target = m_image.rgba.subspan(cell * 4, 4);
            float const alpha = target[3] / 255.f;
            std::array difference{ink.r - m_below[cell][0], ink.g - m_below[cell][1], ink.b - m_below[cell][2]};
            float numerator = 0.f, denominator = 0.f;
            for (int channel = 0; channel < 3; ++channel) {
                numerator += (target[channel] * alpha - m_below[cell][channel]) * difference[channel];
                denominator += difference[channel] * difference[channel];
            }
            if (denominator > 16.f * 16.f) coverage[cell] = std::clamp(numerator / denominator, 0.f, 1.f);
        }
        return coverage;
    }

    void paint(Coverage const& mask, Color ink) {
        for (std::size_t cell = 0; cell < m_cells; ++cell) {
            float const alpha = static_cast<float>(mask.counts[cell]) / kCoverageSamples;
            m_below[cell] = {ink.r * alpha + m_below[cell][0] * (1.f - alpha),
                ink.g * alpha + m_below[cell][1] * (1.f - alpha),
                ink.b * alpha + m_below[cell][2] * (1.f - alpha),
                alpha + m_below[cell][3] * (1.f - alpha)};
            if (m_protectTransparency) m_centers[cell] |= mask.centers[cell];
        }
    }

    double edgeError(Coverage const& mask, std::size_t color,
        std::span<int const> evaluationCells) const {
        auto const ink = m_image.palette[color];
        auto const& later = m_image.coveredLater[color];
        auto residual = [&](int cell) {
            float const alpha = static_cast<float>(mask.counts[cell]) / kCoverageSamples;
            auto const target = m_image.rgba.subspan(cell * 4, 4);
            float const targetAlpha = target[3] / 255.f;
            return std::array{
                ink.r * alpha + m_below[cell][0] * (1.f - alpha) - target[0] * targetAlpha,
                ink.g * alpha + m_below[cell][1] * (1.f - alpha) - target[1] * targetAlpha,
                ink.b * alpha + m_below[cell][2] * (1.f - alpha) - target[2] * targetAlpha};
        };
        double total = 0.;
        for (int cell : evaluationCells) {
            auto const here = residual(cell);
            for (int neighbor : {cell % m_image.width + 1 < m_image.width ? cell + 1 : -1,
                                cell + m_image.width < static_cast<int>(m_cells) ? cell + m_image.width : -1}) {
                if (neighbor < 0 || (!later.empty() && later[neighbor])) continue;
                auto const next = residual(neighbor);
                for (int channel = 0; channel < 3; ++channel) total += std::abs(here[channel] - next[channel]);
            }
        }
        return total;
    }

private:
    void markCenters(ShapeXform const& form, std::vector<std::uint16_t>& centers) const {
        auto const box = xformBox(form, m_image.width, m_image.height);
        constexpr std::array offsets{0.4375f, 0.5f, 0.5625f};
        for (int y = box[1]; y <= box[3]; ++y) for (int x = box[0]; x <= box[2]; ++x) {
            auto& mask = centers[static_cast<std::size_t>(y) * m_image.width + x];
            for (int sy = 0; sy < 3; ++sy) for (int sx = 0; sx < 3; ++sx)
                if (form.contains(x + offsets[sx], y + offsets[sy]))
                    mask |= static_cast<std::uint16_t>(1u << (sy * 3 + sx));
        }
    }

    SurfaceImage const& m_image;
    std::size_t m_cells;
    bool m_protectTransparency = false;
    std::vector<std::array<float, 4>> m_below;
    std::vector<std::uint16_t> m_centers;
};

} // namespace surfaceopt

void optimizePaintSurfaces(std::vector<Primitive>& objects,
    std::vector<std::vector<int>> const& positions, SurfaceImage const& image) {
    std::vector<std::vector<Primitive>> byColor(image.palette.size());
    for (auto const& object : objects) byColor[object.color].push_back(object);
    std::vector<std::size_t> order(image.palette.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return image.ranks[a] < image.ranks[b]; });
    surfaceopt::Composite composite(image);
    for (auto color : order) {
        auto& shapes = byColor[color];
        surfaceopt::ColorRegion const region(image, color, positions[color]);
        auto measureRepaired = [&](std::vector<Primitive>& candidate) {
            auto coverage = composite.measure(candidate);
            if (region.repairInterior(candidate, coverage)) coverage = composite.measure(candidate);
            return coverage;
        };
        auto chosenCoverage = measureRepaired(shapes);
        if (region.invadesProtectedRegions(chosenCoverage)) {
            shapes = vectorizePaintSurface(positions[color], image.width, image.height,
                static_cast<int>(color), image.ranks[color] * kPaintSublayers, image.coveredLater[color], 0.48f, SurfaceFit::MaximalRuns);
            if (shapes.empty()) {
                shapes = packBlocks(positions[color], image.width, image.height, static_cast<int>(color));
                for (auto& shape : shapes) shape.layer = static_cast<std::int16_t>(image.ranks[color] * kPaintSublayers);
            }
            chosenCoverage = measureRepaired(shapes);
        }
        auto const evaluated = region.evaluationCells();
        double const limit = composite.error(chosenCoverage, color, chosenCoverage, evaluated) * surfaceopt::kMaximumErrorRatio +
            evaluated.size() * 3 * surfaceopt::kPixelErrorSlack;
        auto tryCandidate = [&](std::vector<Primitive> candidate) {
            if (candidate.empty() || candidate.size() >= shapes.size()) return;
            auto mask = measureRepaired(candidate);
            if (candidate.size() >= shapes.size() || region.invadesProtectedRegions(mask) ||
                composite.error(mask, color, chosenCoverage, evaluated) > limit) return;
            shapes = std::move(candidate);
            chosenCoverage = std::move(mask);
        };
        if (shapes.size() > 2) {
            for (auto fitting : {SurfaceFit::Adaptive, SurfaceFit::MaximalRuns}) {
                for (float deviation : {0.48f, 0.65f, 0.8f, 1.2f, 1.6f, 2.f, 2.6f, 3.4f}) {
                    tryCandidate(vectorizePaintSurface(positions[color], image.width, image.height,
                        static_cast<int>(color), image.ranks[color] * kPaintSublayers, image.coveredLater[color], deviation, fitting));
                }
            }
            tryCandidate(roundPaintRegions(shapes, positions[color], image.width, image.height,
                static_cast<int>(color), image.ranks[color] * kPaintSublayers, image.coveredLater[color]));
            std::size_t const smoothObjectLimit = shapes.size() * surfaceopt::kMaximumContourCostRatio + 8;
            auto tryContourCandidate = [&](std::vector<Primitive> candidate) {
                if (candidate.empty() || candidate.size() > smoothObjectLimit) return;
                auto mask = measureRepaired(candidate);
                if (candidate.size() > smoothObjectLimit || region.invadesProtectedRegions(mask)) return;
                // a continuous contour may cost more pieces. compare source
                // gradients too: rgb alone can favor a toothed boundary with
                // the same average color coverage as a clean curve.
                double const currentError = composite.error(chosenCoverage, color, chosenCoverage, evaluated);
                double const candidateError = composite.error(mask, color, chosenCoverage, evaluated);
                if (candidateError > limit) return;
                bool const improvesEdges = composite.edgeError(mask, color, evaluated) <
                    composite.edgeError(chosenCoverage, color, evaluated) * 0.99;
                if (candidateError >= currentError * 0.995 && !improvesEdges) return;
                shapes = std::move(candidate);
                chosenCoverage = std::move(mask);
            };
            for (float deviation : {0.65f, 0.8f, 1.2f}) {
                tryContourCandidate(vectorizePaintSurface(positions[color], image.width, image.height,
                    static_cast<int>(color), image.ranks[color] * kPaintSublayers,
                    image.coveredLater[color], deviation, SurfaceFit::Continuous));
            }
            auto const strokeCoverage = composite.estimateStrokeCoverage(color, positions[color], evaluated);
            tryContourCandidate(vectorizeRoundedPaintStrokes(positions[color], image.width, image.height,
                static_cast<int>(color), image.ranks[color] * kPaintSublayers, image.coveredLater[color], strokeCoverage));
            tryContourCandidate(vectorizeRoundedPaintStrokes(positions[color], image.width, image.height,
                static_cast<int>(color), image.ranks[color] * kPaintSublayers, image.coveredLater[color]));
        }
        // local fits litter staircases with tiny fragments (seam repairs,
        // band-end triangles, one-cell patches). a fragment only matters
        // where nothing beneath replaces its color, which the source-error
        // limit already prices exactly. spend whatever error budget the
        // accepted fit left over on deleting the smallest pieces, largest
        // batch first.
        for (int round = 0; round < 10 && shapes.size() > 4; ++round) {
            std::vector<std::size_t> micro;
            for (std::size_t index = 0; index < shapes.size(); ++index)
                if (std::max(shapes[index].width, shapes[index].height) < 2.05f) micro.push_back(index);
            if (micro.empty()) break;
            std::sort(micro.begin(), micro.end(), [&](std::size_t a, std::size_t b) {
                return std::max(shapes[a].width, shapes[a].height) <
                       std::max(shapes[b].width, shapes[b].height);
            });
            bool accepted = false;
            for (std::size_t batch = micro.size(); batch > 0; batch /= 2) {
                std::vector<char> dropped(shapes.size(), 0);
                for (std::size_t index = 0; index < batch; ++index) dropped[micro[index]] = 1;
                std::vector<Primitive> trial;
                trial.reserve(shapes.size() - batch);
                for (std::size_t index = 0; index < shapes.size(); ++index)
                    if (!dropped[index]) trial.push_back(shapes[index]);
                auto mask = measureRepaired(trial);
                if (trial.size() >= shapes.size() || region.invadesProtectedRegions(mask) ||
                    composite.error(mask, color, chosenCoverage, evaluated) > limit) continue;
                shapes = std::move(trial);
                chosenCoverage = std::move(mask);
                accepted = true;
                break;
            }
            if (!accepted) break;
        }
        composite.paint(chosenCoverage, image.palette[color]);
    }
    objects.clear();
    for (auto color : order) objects.insert(objects.end(), byColor[color].begin(), byColor[color].end());
    std::stable_sort(objects.begin(), objects.end(), [](auto const& a, auto const& b) { return a.layer < b.layer; });
    mergePaintSolids(objects);
}

} // namespace paimon::gifimport
