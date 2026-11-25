#include "GifSurfaceVectorizer.hpp"

#include "GifShapeRaster.hpp"
#include "GifVectorMath.hpp"
#include "GifArtVectorizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <array>

namespace paimon::gifimport {
namespace surface {

struct RowSpan {
    int left = 0;
    int right = 0;
    int own = 0;
    int ownLeft = -1;
    int ownRight = -1;
    int strip = -1;
};

struct Strip {
    int firstRow = 0;
    int own = 0;
    std::vector<RowSpan> rows;
};

struct Line {
    float slope = 0.f;
    float intercept = 0.f;
    float at(float row) const { return intercept + slope * row; }
};

struct Band {
    float top = 0.f;
    float bottom = 0.f;
    Line left;
    Line right;
};

std::pair<float, float> boundaryInterval(RowSpan const& row, bool right, float deviation) {
    if (!row.own) return {row.left - deviation, row.right + deviation};
    // tight fits preserve every visible center. loose fits (deviation > 0.5)
    // may move a visible edge inward by up to one cell: the source edge is a
    // smooth curve, not the quantized staircase, so the optimizer's
    // source-error gate -- not the cell grid -- decides whether the straighter
    // line wins.
    float const inward = deviation > 0.5f ? std::min(deviation, 1.0f)
                                          : std::min(deviation, 0.48f);
    return right ? std::pair{row.ownRight - inward, row.right + deviation}
                 : std::pair{row.left - deviation, row.ownLeft + inward};
}

bool boundedOutline(std::vector<Primitive> const& shapes, std::vector<std::uint8_t> const& permitted,
    int width, int height, float deviation);

std::vector<Strip> buildStrips(std::vector<std::uint8_t> const& own,
                               std::vector<std::uint8_t> const& permitted,
                               int width, int height) {
    std::vector<Strip> strips;
    std::vector<RowSpan> previous;
    for (int y = 0; y < height; ++y) {
        std::vector<RowSpan> current;
        for (int x = 0; x < width;) {
            auto const row = static_cast<std::size_t>(y) * width;
            if (!permitted[row + x]) { ++x; continue; }
            RowSpan span;
            span.left = x;
            while (x < width && permitted[row + x]) {
                if (own[row + x]) {
                    if (span.ownLeft < 0) span.ownLeft = x;
                    span.ownRight = x + 1;
                    ++span.own;
                }
                ++x;
            }
            span.right = x;
            current.push_back(span);
        }
        std::vector<int> parent(current.size(), -1), parentCount(current.size(), 0);
        std::vector<int> childCount(previous.size(), 0);
        std::size_t before = 0, next = 0;
        while (before < previous.size() && next < current.size()) {
            auto const& a = previous[before];
            auto const& b = current[next];
            if (std::max(a.left, b.left) < std::min(a.right, b.right)) {
                parent[next] = static_cast<int>(before);
                ++parentCount[next]; ++childCount[before];
            }
            if (a.right <= b.right) ++before;
            else ++next;
        }
        for (std::size_t index = 0; index < current.size(); ++index) {
            auto& span = current[index];
            int const ancestor = parent[index];
            if (ancestor >= 0 && parentCount[index] == 1 && childCount[ancestor] == 1)
                span.strip = previous[ancestor].strip;
            else {
                span.strip = static_cast<int>(strips.size());
                strips.push_back({y, 0, {}});
            }
            auto& strip = strips[span.strip];
            strip.own += span.own;
            strip.rows.push_back(span);
        }
        previous = std::move(current);
    }
    return strips;
}

Line fitLine(Strip const& strip, int first, int last, bool right) {
    float const middle = (first + last) * 0.5f;
    double sum = 0., covariance = 0., variance = 0.;
    for (int index = first; index <= last; ++index) {
        float const value = right ? strip.rows[index].right : strip.rows[index].left;
        float const dy = index - middle;
        sum += value; covariance += value * dy; variance += dy * dy;
    }
    float const slope = variance > 0. ? static_cast<float>(covariance / variance) : 0.f;
    return {slope, static_cast<float>(sum / (last - first + 1)) -
        slope * (strip.firstRow + middle + 0.5f)};
}

// boundary positions hidden under later colors form intervals, not exact edge
// targets. clip the feasible (slope, intercept) polygon to fit one straight
// boundary through all of them.
std::optional<Line> fitBoundary(Strip const& strip, int first, int last, bool right,
                                Line preferred, float deviation) {
    float const middle = strip.firstRow + (first + last) * 0.5f + 0.5f;
    float const scale = std::max(1.f, (last - first) * 0.5f);
    bool fits = true;
    float extent = 1.f;
    for (int index = first; index <= last; ++index) {
        auto const [low, high] = boundaryInterval(strip.rows[index], right, deviation);
        float const value = preferred.at(strip.firstRow + index + 0.5f);
        fits &= value >= low && value <= high;
        extent = std::max(extent, static_cast<float>(strip.rows[index].right));
    }
    if (fits) return preferred;
    std::vector<Point> polygon{{-2.f * extent, -extent}, {2.f * extent, -extent},
        {2.f * extent, 2.f * extent}, {-2.f * extent, 2.f * extent}};
    auto clip = [&](float row, float limit, bool lower) {
        std::vector<Point> clipped;
        auto distance = [&](Point point) {
            float const difference = point.x * row + point.y - limit;
            return lower ? difference : -difference;
        };
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            auto const a = polygon[index], b = polygon[(index + 1) % polygon.size()];
            float const da = distance(a), db = distance(b);
            if (da >= 0.f) clipped.push_back(a);
            if ((da >= 0.f) != (db >= 0.f)) {
                float const along = da / (da - db);
                clipped.push_back({a.x + along * (b.x - a.x), a.y + along * (b.y - a.y)});
            }
        }
        polygon = std::move(clipped);
    };
    for (int index = first; index <= last && !polygon.empty(); ++index) {
        float const row = (strip.firstRow + index + 0.5f - middle) / scale;
        auto const [low, high] = boundaryInterval(strip.rows[index], right, deviation);
        clip(row, low, true);
        clip(row, high, false);
    }
    if (polygon.empty()) return std::nullopt;
    Point const desired{preferred.slope * scale, preferred.at(middle)};
    Point nearest = polygon.front();
    float best = pointDistance(nearest, desired);
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        auto const a = polygon[index], b = polygon[(index + 1) % polygon.size()];
        float const dx = b.x - a.x, dy = b.y - a.y;
        float const length = dx * dx + dy * dy;
        float const along = length > 0.f ? std::clamp(
            ((desired.x - a.x) * dx + (desired.y - a.y) * dy) / length, 0.f, 1.f) : 0.f;
        Point const point{a.x + along * dx, a.y + along * dy};
        float const distance = pointDistance(point, desired);
        if (distance < best) { nearest = point; best = distance; }
    }
    float const slope = nearest.x / scale;
    return Line{slope, nearest.y - slope * middle};
}

std::vector<Band> fitBands(Strip const& strip, float maximumDeviation) {
    std::vector<Band> bands;
    std::vector<std::pair<int, int>> pending{{0, static_cast<int>(strip.rows.size()) - 1}};
    while (!pending.empty()) {
        auto const [first, last] = pending.back();
        pending.pop_back();
        auto const left = fitLine(strip, first, last, false);
        auto const right = fitLine(strip, first, last, true);
        float error = 0.f;
        int split = first;
        for (int index = first; index <= last; ++index) {
            float const y = strip.firstRow + index + 0.5f;
            float const deviation = std::max(std::abs(left.at(y) - strip.rows[index].left),
                std::abs(right.at(y) - strip.rows[index].right));
            if (deviation > error) { error = deviation; split = index; }
        }
        auto const leftBoundary = maximumDeviation > 0.5f
            ? fitBoundary(strip, first, last, false, left, maximumDeviation)
            : error <= maximumDeviation ? std::optional<Line>{left} : std::nullopt;
        auto const rightBoundary = maximumDeviation > 0.5f
            ? fitBoundary(strip, first, last, true, right, maximumDeviation)
            : error <= maximumDeviation ? std::optional<Line>{right} : std::nullopt;
        // a half-cell bound preserves cell centers while discarding staircase edges.
        if ((!leftBoundary || !rightBoundary) && last - first >= 2) {
            split = std::clamp(split, first + 1, last);
            pending.emplace_back(split, last);
            pending.emplace_back(first, split - 1);
            continue;
        }
        bool visible = false;
        for (int index = first; index <= last; ++index) visible |= strip.rows[index].own != 0;
        if (visible) bands.push_back({static_cast<float>(strip.firstRow + first),
            static_cast<float>(strip.firstRow + last + 1), leftBoundary.value_or(left), rightBoundary.value_or(right)});
    }
    return bands;
}

std::optional<Band> fitContinuousBand(Strip const& strip, int first, int last, float deviation) {
    auto const left = fitBoundary(strip, first, last, false, fitLine(strip, first, last, false), deviation);
    if (!left) return std::nullopt;
    auto const right = fitBoundary(strip, first, last, true, fitLine(strip, first, last, true), deviation);
    if (!right) return std::nullopt;
    return Band{static_cast<float>(strip.firstRow + first), static_cast<float>(strip.firstRow + last + 1), *left, *right};
}

// find the longest feasible prefix before starting another piece. splitting a
// whole curve at its largest residual can needlessly cut straight runs.
std::vector<Band> fitMaximalBands(Strip const& strip, float deviation) {
    std::vector<Band> bands;
    int const size = static_cast<int>(strip.rows.size());
    for (int first = 0; first < size;) {
        int last = first, rejected = size;
        auto chosen = fitContinuousBand(strip, first, first, deviation);
        for (int step = 1; first + step < size; step *= 2) {
            int const end = first + step;
            auto candidate = fitContinuousBand(strip, first, end, deviation);
            if (!candidate) { rejected = end; break; }
            last = end; chosen = candidate;
        }
        if (rejected == size) {
            auto candidate = fitContinuousBand(strip, first, size - 1, deviation);
            if (candidate) { last = size - 1; chosen = candidate; }
        }
        while (last + 1 < rejected) {
            int const middle = last + (rejected - last) / 2;
            auto candidate = fitContinuousBand(strip, first, middle, deviation);
            if (candidate) { last = middle; chosen = candidate; }
            else rejected = middle;
        }
        bool visible = false;
        for (int row = first; row <= last; ++row) visible |= strip.rows[row].own != 0;
        if (visible && chosen) bands.push_back(*chosen);
        first = last + 1;
    }
    return bands;
}

struct BoundaryConstraint {
    float row;
    std::pair<float, float> left;
    std::pair<float, float> right;
};

struct BandJoin {
    float row;
    float left;
    float right;
};

// filter the quantization staircase before fitting it. all segments use the
// same join positions, so independently fitted endpoints cannot leave teeth.
std::vector<float> smoothBoundary(Strip const& strip, bool right, float deviation) {
    std::vector<float> values;
    values.reserve(strip.rows.size());
    for (auto const& row : strip.rows)
        values.push_back(static_cast<float>(right ? row.right : row.left));
    constexpr std::array weights{1.f, 4.f, 6.f, 4.f, 1.f};
    for (int pass = 0; pass < 2; ++pass) {
        auto filtered = values;
        for (int index = 0; index < static_cast<int>(values.size()); ++index) {
            float sum = 0.f, weight = 0.f;
            for (int offset = -2; offset <= 2; ++offset) {
                int const neighbor = index + offset;
                if (neighbor < 0 || neighbor >= static_cast<int>(values.size())) continue;
                sum += values[neighbor] * weights[offset + 2];
                weight += weights[offset + 2];
            }
            auto const interval = boundaryInterval(strip.rows[index], right, deviation);
            filtered[index] = std::clamp(sum / weight, interval.first, interval.second);
        }
        values = std::move(filtered);
    }
    return values;
}

std::vector<Band> fitContinuousBands(Strip const& strip, float deviation) {
    auto const left = smoothBoundary(strip, false, deviation);
    auto const right = smoothBoundary(strip, true, deviation);
    int const size = static_cast<int>(strip.rows.size());
    if (size == 0) return {};
    std::vector<BandJoin> joins;
    joins.reserve(size + 1);
    auto atJoin = [&](std::vector<float> const& values, int row) {
        if (size == 1) return values.front();
        if (row == 0) return values[0] - (values[1] - values[0]) * 0.5f;
        if (row == size) return values[size - 1] + (values[size - 1] - values[size - 2]) * 0.5f;
        return (values[row - 1] + values[row]) * 0.5f;
    };
    for (int row = 0; row <= size; ++row)
        joins.push_back({static_cast<float>(strip.firstRow + row), atJoin(left, row), atJoin(right, row)});

    std::vector<Band> bands;
    std::vector<std::pair<int, int>> pending{{0, size}};
    float const tolerance = std::min(0.35f, deviation * 0.5f);
    while (!pending.empty()) {
        auto const [first, last] = pending.back();
        pending.pop_back();
        auto const a = joins[first], b = joins[last];
        float const dy = b.row - a.row;
        Line const l{(b.left - a.left) / dy, a.left - (b.left - a.left) / dy * a.row};
        Line const r{(b.right - a.right) / dy, a.right - (b.right - a.right) / dy * a.row};
        float error = 0.f;
        int split = first;
        for (int index = first + 1; index < last; ++index) {
            auto const point = joins[index];
            float const distance = std::max(std::abs(l.at(point.row) - point.left) / std::hypot(1.f, l.slope),
                std::abs(r.at(point.row) - point.right) / std::hypot(1.f, r.slope));
            if (distance > error) { error = distance; split = index; }
        }
        if (error > tolerance && split > first) {
            pending.emplace_back(split, last);
            pending.emplace_back(first, split);
            continue;
        }
        bool visible = false;
        for (int index = first; index < last; ++index) visible |= strip.rows[index].own != 0;
        if (visible) {
            Band band{a.row, b.row, l, r};
            // a barely tapered strip does not need a separate triangle. keep
            // its centerline and mean width; each endpoint moves at most 0.15
            // of a cell, within the overlap used at adjacent joins.
            if (std::abs((r.slope - l.slope) * dy) <= 0.6f) {
                float const slope = (l.slope + r.slope) * 0.5f;
                float const middle = (a.row + b.row) * 0.5f;
                band.left = {slope, l.at(middle) - slope * middle};
                band.right = {slope, r.at(middle) - slope * middle};
            }
            bands.push_back(band);
        }
    }
    return bands;
}

std::optional<Band> fitParallelBand(Strip const& strip, Band const& band, float deviation) {
    if (std::abs((band.right.slope - band.left.slope) * (band.bottom - band.top)) <= 0.001f) return std::nullopt;
    int const first = static_cast<int>(band.top) - strip.firstRow;
    int const last = static_cast<int>(band.bottom) - strip.firstRow - 1;
    std::vector<BoundaryConstraint> constraints;
    auto add = [&](int index, float row) {
        constraints.push_back({row, boundaryInterval(strip.rows[index], false, deviation),
            boundaryInterval(strip.rows[index], true, deviation)});
    };
    add(first, band.top);
    for (int index = first; index <= last; ++index) add(index, strip.firstRow + index + 0.5f);
    add(last, band.bottom);
    float lowSlope = -std::numeric_limits<float>::infinity();
    float highSlope = std::numeric_limits<float>::infinity();
    // project both feasible boundary corridors onto a common slope interval.
    for (std::size_t i = 0; i < constraints.size(); ++i) for (std::size_t j = i + 1; j < constraints.size(); ++j) {
        float const dy = constraints[j].row - constraints[i].row;
        for (bool right : {false, true}) {
            auto const a = right ? constraints[i].right : constraints[i].left;
            auto const b = right ? constraints[j].right : constraints[j].left;
            lowSlope = std::max(lowSlope, (b.first - a.second) / dy);
            highSlope = std::min(highSlope, (b.second - a.first) / dy);
        }
        if (lowSlope > highSlope) return std::nullopt;
    }
    float const slope = std::clamp((band.left.slope + band.right.slope) * 0.5f, lowSlope, highSlope);
    auto intercept = [&](bool right) -> std::optional<float> {
        float low = -std::numeric_limits<float>::infinity(), high = std::numeric_limits<float>::infinity();
        for (auto const& constraint : constraints) {
            auto const interval = right ? constraint.right : constraint.left;
            low = std::max(low, interval.first - slope * constraint.row);
            high = std::min(high, interval.second - slope * constraint.row);
        }
        if (low > high + 0.0001f) return std::nullopt;
        if (low > high) return (low + high) * 0.5f;
        float const middle = (band.top + band.bottom) * 0.5f;
        return std::clamp((right ? band.right : band.left).at(middle) - slope * middle, low, high);
    };
    auto const left = intercept(false), right = intercept(true);
    if (!left || !right || *right <= *left) return std::nullopt;
    return Band{band.top, band.bottom, {slope, *left}, {slope, *right}};
}

void appendTriangle(std::vector<Primitive>& output, Point a, Point b, Point c,
                    int color, int layer) {
    float const cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(cross) < 0.001f) return;
    if (cross < 0.f) std::swap(b, c);
    float const xAngle = std::atan2(b.y - a.y, b.x - a.x);
    float const yAngle = std::atan2(c.y - a.y, c.x - a.x);
    Primitive triangle{(b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f,
        pointDistance(a, b), pointDistance(a, c), xAngle * 180.f / kPi,
        static_cast<std::uint16_t>(color), PrimitiveKind::Triangle, static_cast<std::int16_t>(layer)};
    triangle.skew = (yAngle - xAngle) * 180.f / kPi - 90.f;
    output.push_back(triangle);
}

void appendBand(std::vector<Primitive>& output, Band const& band, int color, int layer,
    float jointOverlap = 0.f) {
    float const top = band.top, bottom = band.bottom;
    float const leftTop = band.left.at(top), leftBottom = band.left.at(bottom);
    float const rightTop = band.right.at(top), rightBottom = band.right.at(bottom);
    float const topWidth = rightTop - leftTop, bottomWidth = rightBottom - leftBottom;
    float const width = std::min(topWidth, bottomWidth);
    if (width <= 0.f) return;
    float const dy = bottom - top, dx = leftBottom - leftTop;
    Primitive body{(leftTop + leftBottom + width + jointOverlap) * 0.5f, (top + bottom) * 0.5f,
        width + jointOverlap, std::hypot(dx, dy), 0.f, static_cast<std::uint16_t>(color),
        PrimitiveKind::Block, static_cast<std::int16_t>(layer)};
    body.skew = -std::atan2(dx, dy) * 180.f / kPi;
    output.push_back(body);
    if (topWidth > bottomWidth + 0.001f)
        appendTriangle(output, {leftTop + width, top}, {rightTop, top},
            {rightBottom, bottom}, color, layer);
    else if (bottomWidth > topWidth + 0.001f)
        appendTriangle(output, {rightTop, top}, {leftBottom + width, bottom},
            {rightBottom, bottom}, color, layer);
}

std::vector<Primitive> overlapBandJoins(Band band, std::vector<Primitive> original,
    std::vector<std::uint8_t> const& permitted, int width, int height, bool exact) {
    constexpr float overlap = 0.16f;
    float const margin = overlap / (1.f + std::max(std::abs(band.left.slope), std::abs(band.right.slope)));
    band.top -= margin; band.bottom += margin;
    std::vector<Primitive> expanded;
    appendBand(expanded, band, original.front().color, original.front().layer, overlap);
    if (expanded.empty()) return original;
    if (!boundedOutline(expanded, permitted, width, height, 0.65f)) return original;
    if (exact) for (auto const& shape : expanded) {
        bool const foreign = forEachSample(xformOf(shape), width, height, 1, [&](int x, int y) {
            return !permitted[static_cast<std::size_t>(y) * width + x];
        });
        if (foreign) return original;
    }
    return expanded;
}

bool preservesVisibleCenters(Strip const& strip, Band const& band, float deviation = 0.48f) {
    // loose fits may let a smooth edge cross some visible centers; the
    // optimizer repairs interior holes and gates on source error.
    float const tolerance = deviation > 0.5f ? std::min(deviation, 1.0f) : 0.4801f;
    int const first = static_cast<int>(band.top) - strip.firstRow;
    int const last = static_cast<int>(band.bottom) - strip.firstRow - 1;
    for (int index = first; index <= last; ++index) {
        auto const& span = strip.rows[index];
        if (!span.own) continue;
        float const y = strip.firstRow + index + 0.5f;
        if (band.left.at(y) > span.ownLeft + tolerance ||
            band.right.at(y) < span.ownRight - tolerance) return false;
    }
    return true;
}

std::vector<Primitive> fitSurface(std::vector<std::uint8_t> const& own,
    std::vector<std::uint8_t> const& permitted, int width, int height, int color, int layer,
    float maximumDeviation, SurfaceFit fitting) {
    auto const strips = buildStrips(own, permitted, width, height);
    std::vector<Primitive> output;
    for (auto const& strip : strips) {
        if (!strip.own) continue;
        auto emit = [&](auto&& self, Band band) -> void {
            int const first = static_cast<int>(band.top) - strip.firstRow;
            int const last = static_cast<int>(band.bottom) - strip.firstRow - 1;
            auto cap = [&](Line line, bool right) {
                auto clampEnd = [&](float value, RowSpan const& span) {
                    float const inward = maximumDeviation > 0.5f
                        ? std::min(maximumDeviation, 1.0f) : std::min(maximumDeviation, 0.48f);
                    float const lower = right && span.own ? span.ownRight - inward : span.left - 0.5f;
                    float const upper = !right && span.own ? span.ownLeft + inward : span.right + 0.5f;
                    return std::clamp(value, lower, upper);
                };
                float const top = clampEnd(line.at(band.top), strip.rows[first]);
                float const bottom = clampEnd(line.at(band.bottom), strip.rows[last]);
                float const slope = (bottom - top) / (band.bottom - band.top);
                return Line{slope, top - slope * band.top};
            };
            if (maximumDeviation > 0.5f) {
                band.left = cap(band.left, false); band.right = cap(band.right, true);
            }
            std::vector<Primitive> pieces;
            float const outlineTolerance = maximumDeviation > 0.5f ? 1.3f : 0.65f;
            if (auto parallel = fitParallelBand(strip, band, maximumDeviation)) {
                appendBand(pieces, *parallel, color, layer);
                if (boundedOutline(pieces, permitted, width, height, outlineTolerance)) band = *parallel;
                else pieces.clear();
            }
            if (pieces.empty()) appendBand(pieces, band, color, layer);
            if (pieces.empty() || !preservesVisibleCenters(strip, band, maximumDeviation) ||
                !boundedOutline(pieces, permitted, width, height, outlineTolerance)) {
                if (last > first) {
                    int const middle = (first + last + 1) / 2;
                    for (auto const [begin, end] : {std::pair{first, middle}, std::pair{middle, last + 1}}) {
                        Strip part{strip.firstRow + begin, 0, {strip.rows.begin() + begin, strip.rows.begin() + end}};
                        for (auto const& row : part.rows) part.own += row.own;
                        for (auto const& smaller : fitBands(part, std::min(maximumDeviation, 0.8f))) self(self, smaller);
                    }
                    return;
                }
                std::vector<int> cells;
                for (int y = static_cast<int>(band.top); y < static_cast<int>(band.bottom); ++y) {
                    auto const& span = strip.rows[y - strip.firstRow];
                    for (int x = span.left; x < span.right; ++x)
                        if (own[static_cast<std::size_t>(y) * width + x]) cells.push_back(y * width + x);
                }
                pieces = packBlocks(cells, width, height, color, permitted);
                for (auto& piece : pieces) piece.layer = static_cast<std::int16_t>(layer);
            }
            bool const hasJoin = strips.size() > 1 || pieces.size() > 1 || band.top > strip.firstRow ||
                band.bottom < strip.firstRow + static_cast<int>(strip.rows.size());
            if (!pieces.empty() && hasJoin)
                pieces = overlapBandJoins(band, std::move(pieces), permitted, width, height, maximumDeviation <= 0.5f);
            output.insert(output.end(), pieces.begin(), pieces.end());
        };
        if (fitting == SurfaceFit::Continuous) {
            for (auto const& band : fitContinuousBands(strip, maximumDeviation)) {
                std::vector<Primitive> pieces;
                appendBand(pieces, band, color, layer);
                if (pieces.empty() || !boundedOutline(pieces, permitted, width, height, 1.3f)) {
                    // a short steep end can fail in this orientation. refit
                    // only that interval, keeping the other shared joins.
                    emit(emit, band);
                    continue;
                }
                pieces = overlapBandJoins(band, std::move(pieces), permitted, width, height, false);
                output.insert(output.end(), pieces.begin(), pieces.end());
            }
            continue;
        }
        auto const bands = fitting == SurfaceFit::MaximalRuns ? fitMaximalBands(strip, maximumDeviation)
                                                              : fitBands(strip, maximumDeviation);
        for (auto const& band : bands) emit(emit, band);
    }
    if (maximumDeviation > 0.5f) return output;
    std::vector<std::uint8_t> paints(own.size(), 0);
    bool foreign = false;
    for (auto const& object : output) {
        forEachSample(xformOf(object), width, height, 1, [&](int x, int y) {
            auto const cell = static_cast<std::size_t>(y) * width + x;
            paints[cell] = 1;
            foreign |= !permitted[cell];
            return false;
        });
    }
    if (foreign) return {};
    std::vector<int> missing;
    for (std::size_t cell = 0; cell < own.size(); ++cell)
        if (own[cell] && !paints[cell]) missing.push_back(static_cast<int>(cell));
    auto repairs = packBlocks(missing, width, height, color, permitted);
    for (auto& object : repairs) object.layer = static_cast<std::int16_t>(layer);
    output.insert(output.end(), repairs.begin(), repairs.end());
    return output;
}

struct ComponentMoments {
    double x = 0., y = 0., xx = 0., xy = 0., yy = 0.;
};

ComponentMoments measureComponent(std::vector<int> const& component, int width) {
    ComponentMoments result;
    for (int cell : component) { result.x += cell % width + 0.5; result.y += cell / width + 0.5; }
    result.x /= component.size(); result.y /= component.size();
    for (int cell : component) {
        double const dx = cell % width + 0.5 - result.x, dy = cell / width + 0.5 - result.y;
        result.xx += dx * dx; result.xy += dx * dy; result.yy += dy * dy;
    }
    result.xx /= component.size(); result.xy /= component.size(); result.yy /= component.size();
    return result;
}

std::optional<Primitive> fitEllipse(std::vector<int> const& component,
    std::vector<std::uint8_t> const& permitted, int width, int height, int color, int layer, bool exact) {
    if (component.size() < 32) return std::nullopt;
    auto const [mx, my, xx, xy, yy] = measureComponent(component, width);
    double const spread = std::hypot(xx - yy, 2. * xy);
    float const major = static_cast<float>(4. * std::sqrt(std::max(0., (xx + yy + spread) * 0.5)));
    float const minor = static_cast<float>(4. * std::sqrt(std::max(0., (xx + yy - spread) * 0.5)));
    float const area = major * minor * kPi * 0.25f;
    if (minor < 2.f || major > 256.f || major > minor * 4.f ||
        area < component.size() * 0.8f || area > component.size() * 1.15f) return std::nullopt;
    float const angle = static_cast<float>(0.5 * std::atan2(2. * xy, xx - yy) * 180. / kPi);
    for (float dx : {0.f, -0.125f, 0.125f}) for (float dy : {0.f, -0.125f, 0.125f})
        for (float dw : {0.f, 0.125f, 0.25f, 0.5f, 0.75f, 1.f})
            for (float dh : {0.f, 0.125f, 0.25f, 0.5f, 0.75f, 1.f}) {
                Primitive ellipse{static_cast<float>(mx) + dx, static_cast<float>(my) + dy,
                    major + dw, minor + dh, angle, static_cast<std::uint16_t>(color),
                    PrimitiveKind::Circle, static_cast<std::int16_t>(layer)};
                auto const form = xformOf(ellipse);
                auto const missing = std::count_if(component.begin(), component.end(), [&](int cell) {
                        return !form.contains(cell % width + 0.5f, cell / width + 0.5f);
                    });
                auto const allowance = exact ? 0u : component.size() / 100;
                if (static_cast<std::size_t>(missing) > allowance) continue;
                std::size_t foreign = 0;
                bool const invades = forEachSample(form, width, height, 1, [&](int x, int y) {
                    if (!permitted[static_cast<std::size_t>(y) * width + x]) ++foreign;
                    return foreign > allowance;
                });
                if (!invades) return ellipse;
            }
    return std::nullopt;
}

std::vector<Primitive> fitCapsule(std::vector<int> const& component,
    std::vector<std::uint8_t> const& permitted, int width, int height, int color, int layer, bool exact) {
    if (component.size() < 32) return {};
    auto const [mx, my, xx, xy, yy] = measureComponent(component, width);
    float const angle = static_cast<float>(0.5 * std::atan2(2. * xy, xx - yy));
    float const cosine = std::cos(angle), sine = std::sin(angle);
    float minX = std::numeric_limits<float>::max(), maxX = -minX;
    float minY = minX, maxY = -minY;
    for (int cell : component) {
        float const dx = cell % width + 0.5f - static_cast<float>(mx);
        float const dy = cell / width + 0.5f - static_cast<float>(my);
        float const x = dx * cosine + dy * sine, y = -dx * sine + dy * cosine;
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    float const length = maxX - minX, diameter = maxY - minY;
    float const area = length * diameter - (1.f - kPi * 0.25f) * diameter * diameter;
    if (diameter < 2.f || length < diameter * 1.4f || length > 256.f ||
        area < component.size() * 0.85f || area > component.size() * 1.1f) return {};
    float cx = static_cast<float>(mx) + (minX + maxX) * 0.5f * cosine - (minY + maxY) * 0.5f * sine;
    float cy = static_cast<float>(my) + (minX + maxX) * 0.5f * sine + (minY + maxY) * 0.5f * cosine;
    std::array<float, 5> parameters{cx, cy, angle, length - diameter, diameter};
    if (!exact) {
        std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height, 0);
        for (int cell : component) mask[cell] = 1;
        std::vector<Point> boundary;
        for (int cell : component) {
            int const x = cell % width, y = cell / width;
            if (x == 0 || !mask[cell - 1]) boundary.push_back({static_cast<float>(x), y + 0.5f});
            if (x + 1 == width || !mask[cell + 1]) boundary.push_back({x + 1.f, y + 0.5f});
            if (y == 0 || !mask[cell - width]) boundary.push_back({x + 0.5f, static_cast<float>(y)});
            if (y + 1 == height || !mask[cell + width]) boundary.push_back({x + 0.5f, y + 1.f});
        }
        auto residual = [&](std::array<float, 5> const& p) {
            if (p[3] <= 0.f || p[4] <= 1.f) return std::numeric_limits<double>::infinity();
            float const ca = std::cos(p[2]), sa = std::sin(p[2]);
            double sum = 0.;
            for (auto const point : boundary) {
                float const dx = point.x - p[0], dy = point.y - p[1];
                float const u = std::max(0.f, std::abs(dx * ca + dy * sa) - p[3] * 0.5f);
                float const v = -dx * sa + dy * ca;
                float const distance = std::abs(std::hypot(u, v) - p[4] * 0.5f);
                sum += distance < 1.f ? distance * distance : 2.f * distance - 1.f;
            }
            return sum;
        };
        double best = residual(parameters);
        for (float step : {1.f, 0.5f, 0.25f, 0.125f}) for (int pass = 0; pass < 4; ++pass)
            for (std::size_t axis = 0; axis < parameters.size(); ++axis) for (float sign : {-1.f, 1.f}) {
                auto candidate = parameters;
                candidate[axis] += sign * step * (axis == 2 ? kPi / 180.f : 1.f);
                double const error = residual(candidate);
                if (error < best) { parameters = candidate; best = error; }
            }
    }
    cx = parameters[0]; cy = parameters[1];
    float const fittedCosine = std::cos(parameters[2]), fittedSine = std::sin(parameters[2]);
    for (float dw : {0.01f, 0.125f, 0.25f, 0.5f, 0.75f, 1.f})
        for (float dh : {0.01f, 0.125f, 0.25f, 0.5f, 0.75f, 1.f}) {
            float const d = parameters[4] + dh, span = parameters[3] + dw - dh;
            Primitive body{cx, cy, span, d, parameters[2] * 180.f / kPi,
                static_cast<std::uint16_t>(color), PrimitiveKind::Stroke, static_cast<std::int16_t>(layer)};
            Primitive cap{cx - span * 0.5f * fittedCosine, cy - span * 0.5f * fittedSine, d, d, 0.f,
                static_cast<std::uint16_t>(color), PrimitiveKind::Circle, static_cast<std::int16_t>(layer)};
            std::vector<Primitive> shapes{body, cap};
            cap.x = cx + span * 0.5f * fittedCosine; cap.y = cy + span * 0.5f * fittedSine;
            shapes.push_back(cap);
            auto const forms = xformsOf(shapes);
            auto const missing = std::count_if(component.begin(), component.end(), [&](int cell) {
                    return std::none_of(forms.begin(), forms.end(), [&](auto const& form) {
                        return form.contains(cell % width + 0.5f, cell / width + 0.5f);
                    });
                });
            auto const allowance = exact ? 0u : component.size() / 50;
            if (static_cast<std::size_t>(missing) > allowance) continue;
            bool invades = false;
            std::size_t foreign = 0;
            for (auto const& form : forms) invades |= forEachSample(form, width, height, 1, [&](int x, int y) {
                if (!permitted[static_cast<std::size_t>(y) * width + x]) ++foreign;
                return foreign > allowance;
            });
            if (!invades && boundedOutline(shapes, permitted, width, height, 0.65f)) return shapes;
        }
    return {};
}

// multi-lobed round regions (clouds, hills, cheeks) are not one ellipse, but
// decompose into a few inscribed discs: place a disc at the deepest uncovered
// cell, shrink it until it stops invading foreign cells, repeat. whatever the
// discs cannot cover stays in `uncovered` for the band fitter, so this only
// ever replaces band fragments -- never invents coverage.
std::vector<Primitive> fitBlobDiscs(std::vector<int> const& component,
    std::vector<std::uint8_t> const& permitted, int width, int height, int color, int layer,
    std::vector<int>& uncovered) {
    auto const box = bounds(component, width);
    int const pad = 2;
    int const boxWidth = box[2] - box[0] + 1 + pad * 2;
    int const boxHeight = box[3] - box[1] + 1 + pad * 2;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(boxWidth) * boxHeight, 0);
    for (int cell : component) {
        mask[static_cast<std::size_t>(cell / width - box[1] + pad) * boxWidth +
             (cell % width - box[0] + pad)] = 1;
    }
    auto const distance = distanceField(mask, boxWidth, boxHeight);
    std::vector<std::uint8_t> covered(component.size(), 0);
    std::vector<std::uint8_t> banned(static_cast<std::size_t>(width) * height, 0);
    std::vector<Primitive> discs;
    std::size_t coveredCount = 0;
    float const allowance = static_cast<float>(component.size()) / 50.f + 2.f;
    for (int iteration = 0; iteration < 40; ++iteration) {
        int bestCell = -1;
        float bestDistance = 0.f;
        for (std::size_t index = 0; index < component.size(); ++index) {
            int const cell = component[index];
            if (covered[index] || banned[static_cast<std::size_t>(cell)]) continue;
            float const value = distance[
                static_cast<std::size_t>(cell / width - box[1] + pad) * boxWidth +
                (cell % width - box[0] + pad)];
            if (value > bestDistance) { bestDistance = value; bestCell = cell; }
        }
        if (bestCell < 0 || bestDistance < 4.5f) break;
        float const centerX = bestCell % width + 0.5f;
        float const centerY = bestCell / width + 0.5f;
        bool placed = false;
        for (float scale : {1.f, 0.88f, 0.76f}) {
            float const radius = bestDistance * scale;
            Primitive disc{centerX, centerY, radius * 2.f, radius * 2.f, 0.f,
                static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
                static_cast<std::int16_t>(layer)};
            auto const form = xformOf(disc);
            std::size_t foreign = 0;
            forEachSample(form, width, height, 1, [&](int x, int y) {
                if (!permitted[static_cast<std::size_t>(y) * width + x]) ++foreign;
                return foreign > static_cast<std::size_t>(allowance);
            });
            if (foreign > static_cast<std::size_t>(allowance)) continue;
            std::size_t fresh = 0;
            for (std::size_t index = 0; index < component.size(); ++index) {
                if (covered[index]) continue;
                int const cell = component[index];
                if (form.contains(cell % width + 0.5f, cell / width + 0.5f)) ++fresh;
            }
            if (fresh < 12) continue;
            for (std::size_t index = 0; index < component.size(); ++index) {
                if (covered[index]) continue;
                int const cell = component[index];
                if (form.contains(cell % width + 0.5f, cell / width + 0.5f)) {
                    covered[index] = 1;
                    ++coveredCount;
                }
            }
            discs.push_back(disc);
            placed = true;
            break;
        }
        if (!placed) banned[static_cast<std::size_t>(bestCell)] = 1;
        if (coveredCount >= component.size() * 97 / 100) break;
    }
    if (discs.empty() || coveredCount < component.size() * 80 / 100) return {};
    for (std::size_t index = 0; index < component.size(); ++index)
        if (!covered[index]) uncovered.push_back(component[index]);
    return discs;
}

enum class RoundedFit { WholeRegions, FragmentDiscs };

std::vector<Primitive> fitRoundedShapes(std::vector<int> const& positions,
    std::vector<std::uint8_t> const& permitted, int width, int height, int color, int layer, bool exact,
    std::vector<int>& remaining, RoundedFit fitting = RoundedFit::FragmentDiscs) {
    auto const components = connectedComponents(positions, width, height);
    if (components.size() > 128) { remaining = positions; return {}; }
    std::vector<Primitive> output;
    for (auto const& component : components) {
        if (component.size() < 8) {
            remaining.insert(remaining.end(), component.begin(), component.end());
            continue;
        }
        auto ellipse = fitEllipse(component, permitted, width, height, color, layer, exact);
        if (ellipse) output.push_back(*ellipse);
        else {
            auto capsule = fitCapsule(component, permitted, width, height, color, layer, exact);
            if (!capsule.empty()) {
                output.insert(output.end(), capsule.begin(), capsule.end());
                continue;
            }
            if (!exact && fitting == RoundedFit::FragmentDiscs && component.size() >= 96) {
                std::vector<int> uncovered;
                auto discs = fitBlobDiscs(component, permitted, width, height, color, layer, uncovered);
                if (!discs.empty()) {
                    output.insert(output.end(), discs.begin(), discs.end());
                    remaining.insert(remaining.end(), uncovered.begin(), uncovered.end());
                    continue;
                }
            }
            remaining.insert(remaining.end(), component.begin(), component.end());
        }
    }
    return output;
}

bool boundedOutline(std::vector<Primitive> const& shapes, std::vector<std::uint8_t> const& permitted,
    int width, int height, float deviation) {
    auto fits = [&](Point point, float tolerance) {
        int const cx = static_cast<int>(std::floor(point.x)), cy = static_cast<int>(std::floor(point.y));
        int const reach = static_cast<int>(std::ceil(tolerance));
        for (int y = std::max(0, cy - reach); y <= std::min(height - 1, cy + reach); ++y)
            for (int x = std::max(0, cx - reach); x <= std::min(width - 1, cx + reach); ++x) {
                if (!permitted[static_cast<std::size_t>(y) * width + x]) continue;
                float const dx = std::max({x - point.x, 0.f, point.x - x - 1.f});
                float const dy = std::max({y - point.y, 0.f, point.y - y - 1.f});
                if (dx * dx + dy * dy <= tolerance * tolerance) return true;
            }
        return false;
    };
    for (auto const& shape : shapes) {
        // a fitted capsule has a continuous round outline. its antialiased
        // source edge may span a wider color band than polygon corners.
        float const tolerance = shape.kind == PrimitiveKind::Circle || shape.kind == PrimitiveKind::Stroke
            ? std::max(deviation, 1.25f) : deviation;
        auto const form = xformOf(shape);
        auto transform = [&](float x, float y) {
            return Point{form.x + x * form.cosine - y * form.ySine,
                form.y + x * form.sine + y * form.yCosine};
        };
        if (shape.kind == PrimitiveKind::Circle) {
            int const samples = std::max(16, static_cast<int>(std::ceil(kPi * std::max(shape.width, shape.height) * 2.f)));
            for (int sample = 0; sample < samples; ++sample) {
                float const angle = sample * 2.f * kPi / samples;
                if (!fits(transform(std::cos(angle) * shape.width * 0.5f, std::sin(angle) * shape.height * 0.5f), tolerance)) return false;
            }
        } else {
            std::vector<Point> vertices{transform(-shape.width * 0.5f, -shape.height * 0.5f),
                transform(shape.width * 0.5f, -shape.height * 0.5f)};
            if (shape.kind != PrimitiveKind::Triangle && shape.kind != PrimitiveKind::WideTriangle)
                vertices.push_back(transform(shape.width * 0.5f, shape.height * 0.5f));
            vertices.push_back(transform(-shape.width * 0.5f, shape.height * 0.5f));
            for (std::size_t i = 0; i < vertices.size(); ++i) {
                auto const a = vertices[i], b = vertices[(i + 1) % vertices.size()];
                int const samples = std::max(1, static_cast<int>(std::ceil(pointDistance(a, b) * 2.f)));
                for (int sample = 0; sample <= samples; ++sample) {
                    float const along = static_cast<float>(sample) / samples;
                    if (!fits({a.x + (b.x - a.x) * along, a.y + (b.y - a.y) * along}, tolerance)) return false;
                }
            }
        }
    }
    return true;
}

} // namespace surface

std::vector<Primitive> roundPaintRegions(std::vector<Primitive> const& existing,
    std::vector<int> const& positions, int width, int height, int color, int layer,
    std::vector<std::uint8_t> const& coveredLater) {
    std::vector<std::uint8_t> permitted(static_cast<std::size_t>(width) * height, 0);
    for (int cell : positions) permitted[cell] = 1;
    if (coveredLater.size() == permitted.size())
        for (std::size_t cell = 0; cell < permitted.size(); ++cell) permitted[cell] |= coveredLater[cell];
    std::vector<int> remaining;
    auto rounded = surface::fitRoundedShapes(positions, permitted, width, height, color, layer, false, remaining);
    if (rounded.empty()) return {};
    std::vector<std::uint8_t> fitted(permitted.size(), 0);
    for (int cell : positions) fitted[cell] = 1;
    for (int cell : remaining) fitted[cell] = 0;
    std::vector<int> fittedPositions;
    for (int cell : positions) if (fitted[cell]) fittedPositions.push_back(cell);
    std::vector<std::array<int, 4>> regions;
    for (auto const& component : connectedComponents(fittedPositions, width, height)) regions.push_back(bounds(component, width));
    for (auto const& object : existing) {
        auto const form = xformOf(object);
        bool const replaced = std::any_of(regions.begin(), regions.end(), [&](auto const& box) {
            return form.x - form.extentX >= box[0] - 1.f && form.x + form.extentX <= box[2] + 2.f &&
                form.y - form.extentY >= box[1] - 1.f && form.y + form.extentY <= box[3] + 2.f;
        });
        if (!replaced) rounded.push_back(object);
    }
    return rounded;
}

std::vector<Primitive> vectorizePaintSurface(std::vector<int> const& positions,
    int width, int height, int color, int layer, std::vector<std::uint8_t> const& coveredLater,
    float maximumDeviation, SurfaceFit fitting) {
    if (positions.empty() || width <= 0 || height <= 0) return {};
    std::vector<std::uint8_t> own(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) own[static_cast<std::size_t>(position)] = 1;
    auto permitted = own;
    if (coveredLater.size() == own.size())
        for (std::size_t cell = 0; cell < own.size(); ++cell) permitted[cell] |= coveredLater[cell];
    auto const outlineMask = permitted;
    float const outlineGate = maximumDeviation > 0.5f ? 1.3f : 0.75f;
    auto fitBest = [&](std::vector<std::uint8_t> const& fitOwn,
                       std::vector<std::uint8_t> const& fitPermitted,
                       std::vector<Primitive> extra) {
        auto output = surface::fitSurface(fitOwn, fitPermitted, width, height, color, layer, maximumDeviation, fitting);
        std::vector<std::uint8_t> transposedOwn(fitOwn.size()), transposedPermitted(fitOwn.size());
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            auto const source = static_cast<std::size_t>(y) * width + x;
            auto const target = static_cast<std::size_t>(x) * height + y;
            transposedOwn[target] = fitOwn[source];
            transposedPermitted[target] = fitPermitted[source];
        }
        auto transposed = surface::fitSurface(transposedOwn, transposedPermitted, height, width, color, layer, maximumDeviation, fitting);
        if (!transposed.empty() && (output.empty() || transposed.size() < output.size())) {
            for (auto& object : transposed) {
                std::swap(object.x, object.y);
                std::swap(object.width, object.height);
                object.rotation = -object.rotation - object.skew;
            }
            output = std::move(transposed);
        }
        if (output.empty() && extra.empty()) return std::vector<Primitive>{};
        output.insert(output.end(), extra.begin(), extra.end());
        return surface::boundedOutline(output, outlineMask, width, height, outlineGate)
            ? output : std::vector<Primitive>{};
    };
    std::vector<int> remaining;
    auto rounded = surface::fitRoundedShapes(positions, permitted, width, height, color, layer,
        maximumDeviation <= 0.5f, remaining, fitting == SurfaceFit::Continuous
            ? surface::RoundedFit::WholeRegions : surface::RoundedFit::FragmentDiscs);
    if (remaining.empty()) return surface::boundedOutline(rounded, permitted, width, height, 0.75f) ? rounded : std::vector<Primitive>{};
    if (maximumDeviation <= 0.5f) { rounded.clear(); remaining = positions; }
    bool const usedDiscs = std::any_of(rounded.begin(), rounded.end(), [](Primitive const& shape) {
        return shape.kind == PrimitiveKind::Circle;
    });
    if (!rounded.empty()) {
        auto fullOwn = own;
        auto fullPermitted = permitted;
        std::fill(own.begin(), own.end(), 0);
        for (int cell : remaining) own[cell] = 1;
        permitted = own;
        if (coveredLater.size() == own.size())
            for (std::size_t cell = 0; cell < own.size(); ++cell) permitted[cell] |= coveredLater[cell];
        auto withRounded = fitBest(own, permitted, rounded);
        // discs buy round edges, never extra objects: if bands alone fit the
        // whole color in fewer pieces, the disc variant loses.
        if (usedDiscs && !withRounded.empty()) {
            auto plain = fitBest(fullOwn, fullPermitted, {});
            if (!plain.empty() && plain.size() <= withRounded.size()) return plain;
        }
        return withRounded;
    }
    return fitBest(own, permitted, {});
}

} // namespace paimon::gifimport
