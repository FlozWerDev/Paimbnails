#include "GifPaintVectorizer.hpp"

#include "GifArtVectorizer.hpp"
#include "GifShapeRaster.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace paimon::gifimport {

namespace {

constexpr float kBandWidth = 3.f;
constexpr float kOvershoot = 0.72f;
constexpr float kFreeOvershoot = 0.2f;
constexpr float kSmoothTolerance = 0.9f;
constexpr float kThinRadius = 4.25f;
// past this width the blob is solid area, not stroke.
constexpr int kThickSpan = 5;
// diagonals fill their box; l/s shapes leave half empty and cover below.
constexpr float kPatchSlack = 1.7f;
// min length (in thicknesses) for stroke tracing to pay off.
constexpr float kChainSlenderness = 3.f;
// max strip spill before dropping it.
constexpr float kChainSpill = 0.06f;
// smooth steps on half a neighbor cell: 15% spill, 14° straight/turned cut.
constexpr float kSmoothSpill = 0.15f;
constexpr float kSmoothStraightAngle = 14.f;
// 0.007 is the peak erase spill that keeps geometry in place.
constexpr float kChainEraseSpill = 0.007f;
constexpr float kRepairDiameter = 1.f;
// below this size circles read as speckles: prefer a straight patch.
constexpr float kRoundCapMinThickness = 0.65f;
constexpr float kVisibleRoundCapDiameter = 1.25f;
// holds thin tips back up to 1 cell.
constexpr float kExposedCapGap = 0.65f;
constexpr int kPadding = 2;
// stretched boxes steal room from the next round of merges.
constexpr float kAbsorbSlack = 0.6f;
// every round re-climbs the whole drawing; late ones barely contribute.
constexpr int kAbsorbRounds = 4;

struct Region {
    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;
    std::vector<std::uint8_t> cells;
    std::vector<float> distance;

    bool filled(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        return cells[static_cast<std::size_t>(y) * width + x] != 0;
    }

    bool filledAt(float x, float y) const {
        return filled(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    }

    float distanceAt(float x, float y) const {
        int const cellX = std::clamp(static_cast<int>(std::floor(x)), 0, width - 1);
        int const cellY = std::clamp(static_cast<int>(std::floor(y)), 0, height - 1);
        return distance[static_cast<std::size_t>(cellY) * width + cellX];
    }
};

void computeDistance(Region& region) {
    region.distance = distanceField(region.cells, region.width, region.height);
}

Region buildRegion(std::vector<int> const& component, int sourceWidth) {
    auto const box = bounds(component, sourceWidth);
    Region region;
    region.width = box[2] - box[0] + 1 + kPadding * 2;
    region.height = box[3] - box[1] + 1 + kPadding * 2;
    region.offsetX = box[0] - kPadding;
    region.offsetY = box[1] - kPadding;
    region.cells.assign(static_cast<std::size_t>(region.width) * region.height, 0);
    for (int position : component) {
        int const x = position % sourceWidth - region.offsetX;
        int const y = position / sourceWidth - region.offsetY;
        region.cells[static_cast<std::size_t>(y) * region.width + x] = 1;
    }
    computeDistance(region);
    return region;
}

// square-step thickness, not circles, to split solid area from stroke.
std::vector<int> boardDistance(
    std::vector<std::uint8_t> const& cells,
    int width,
    int height,
    bool borderCounts
) {
    int const unreachable = width + height;
    std::vector<int> distance(cells.size(), 0);
    for (std::size_t index = 0; index < cells.size(); ++index) {
        distance[index] = cells[index] ? unreachable : 0;
    }
    auto relax = [&](int x, int y, std::array<std::pair<int, int>, 4> const& steps) {
        auto const index = static_cast<std::size_t>(y) * width + x;
        if (!cells[index]) return;
        int best = unreachable;
        for (auto const [dx, dy] : steps) {
            int const xx = x + dx;
            int const yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= width || yy >= height) {
                if (borderCounts) best = 0;
                continue;
            }
            best = std::min(best, distance[static_cast<std::size_t>(yy) * width + xx]);
        }
        distance[index] = std::min(distance[index], best + 1);
    };
    constexpr std::array<std::pair<int, int>, 4> kBefore{
        std::pair{-1, -1}, std::pair{0, -1}, std::pair{1, -1}, std::pair{-1, 0}
    };
    constexpr std::array<std::pair<int, int>, 4> kAfter{
        std::pair{1, 1}, std::pair{0, 1}, std::pair{-1, 1}, std::pair{1, 0}
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) relax(x, y, kBefore);
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) relax(x, y, kAfter);
    }
    return distance;
}

// splits one color's solid area into rectangles and its stroke into strips.
std::vector<std::vector<int>> splitByThickness(
    std::vector<int> const& component,
    int width,
    int height,
    int span
) {
    auto const box = bounds(component, width);
    int const localWidth = box[2] - box[0] + 1;
    int const localHeight = box[3] - box[1] + 1;
    if (localWidth < span * 2 || localHeight < span * 2) {
        return connectedComponents(component, width, height);
    }
    std::vector<std::uint8_t> shape(
        static_cast<std::size_t>(localWidth) * localHeight, 0);
    for (int position : component) {
        int const x = position % width - box[0];
        int const y = position / width - box[1];
        shape[static_cast<std::size_t>(y) * localWidth + x] = 1;
    }
    auto const thickness = boardDistance(shape, localWidth, localHeight, true);

    std::vector<std::uint8_t> outside(shape.size(), 1);
    bool hasCore = false;
    for (std::size_t index = 0; index < shape.size(); ++index) {
        if (thickness[index] < span) continue;
        outside[index] = 0;
        hasCore = true;
    }
    if (!hasCore) return connectedComponents(component, width, height);
    auto const reach = boardDistance(outside, localWidth, localHeight, false);

    std::vector<int> thick;
    std::vector<int> slim;
    for (int position : component) {
        int const x = position % width - box[0];
        int const y = position / width - box[1];
        auto const index = static_cast<std::size_t>(y) * localWidth + x;
        (reach[index] <= span ? thick : slim).push_back(position);
    }
    if (slim.empty()) return connectedComponents(component, width, height);
    auto pieces = connectedComponents(thick, width, height);
    for (auto& piece : connectedComponents(slim, width, height)) {
        pieces.push_back(std::move(piece));
    }
    return pieces;
}

std::array<int, 4> shapeBox(Primitive const& shape, int width, int height) {
    return xformBox(xformOf(shape), width, height);
}

// 9 center samples decide foreign-pixel hits.
constexpr float kCenterOffsets[3] = {0.4f, 0.5f, 0.6f};

// free subpixel fringe: foreign center and total area are capped.
bool fitsPaintBoundary(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height,
    bool gridExact = true,
    float spillLimit = -1.f
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    if (box[2] < box[0] || box[3] < box[1]) return true;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            if (permitted[static_cast<std::size_t>(y) * width + x]) continue;
            for (float dy : kCenterOffsets) {
                for (float dx : kCenterOffsets) {
                    if (placed.contains(x + dx, y + dy)) return false;
                }
            }
        }
    }
    return shapeSpill(shape, permitted, width, height) <=
        (spillLimit >= 0.f ? spillLimit : gridExact ? kChainSpill : kSmoothSpill);
}

// 1-cell fringe around the blob: hugs it without crossing the drawing.
std::vector<std::uint8_t> nearCells(
    std::vector<int> const& positions, int width, int height) {
    std::vector<std::uint8_t> nearMask(
        static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) {
        if (position < 0 ||
            position >= static_cast<int>(nearMask.size())) {
            continue;
        }
        int const x = position % width;
        int const y = position / width;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int const xx = x + dx;
                int const yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                nearMask[static_cast<std::size_t>(yy) * width + xx] = 1;
            }
        }
    }
    return nearMask;
}

// the fringe never forgives void; over another color it stays forgiven.
void maskVoid(
    std::vector<std::uint8_t>& mask,
    std::vector<std::uint8_t> const& empty
) {
    if (mask.size() != empty.size()) return;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (empty[i]) mask[i] = 0;
    }
}

// true spill: outside both the permitted area and the fringe.
float shapeFarSpill(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& nearMask,
    int width,
    int height
) {
    constexpr int kSamples = 4;
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    if (box[2] < box[0] || box[3] < box[1]) return 0.f;
    int covered = 0;
    int spilled = 0;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            bool const forgiven =
                (index < permitted.size() && permitted[index]) ||
                (index < nearMask.size() && nearMask[index]);
            for (int sampleY = 0; sampleY < kSamples; ++sampleY) {
                for (int sampleX = 0; sampleX < kSamples; ++sampleX) {
                    if (!placed.contains(
                            static_cast<float>(x) +
                                (sampleX + 0.5f) / kSamples,
                            static_cast<float>(y) +
                                (sampleY + 0.5f) / kSamples)) {
                        continue;
                    }
                    ++covered;
                    spilled += !forgiven;
                }
            }
        }
    }
    return covered > 0 ? static_cast<float>(spilled) / covered : 0.f;
}

// the fringe counts as home only for short chain/band/patch strips.
bool fitsPaintNear(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& nearMask,
    int width,
    int height,
    bool gridExact = true
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    if (box[2] < box[0] || box[3] < box[1]) return true;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if ((index < permitted.size() && permitted[index]) ||
                (index < nearMask.size() && nearMask[index])) {
                continue;
            }
            for (float dy : kCenterOffsets) {
                for (float dx : kCenterOffsets) {
                    if (placed.contains(x + dx, y + dy)) return false;
                }
            }
        }
    }
    return shapeFarSpill(shape, permitted, nearMask, width, height) <=
        (gridExact ? kChainSpill : kSmoothSpill);
}

bool fitsStrokeFringe(Primitive const& shape, std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& fringeMask, int width, int height) {
    return !forEachSample(xformOf(shape), width, height, 4, [&](int x, int y) {
        auto const cell = static_cast<std::size_t>(y / 4) * width + x / 4;
        return !permitted[cell] && !fringeMask[cell];
    });
}

// the fringe only forgives on turned strips; straight demands the exact pixel.
bool fitsPaintOutline(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& nearMask,
    int width,
    int height,
    bool gridExact = true
) {
    float folded = std::fmod(std::abs(shape.rotation), 90.f);
    folded = std::min(folded, 90.f - folded);
    float const straightAngle = gridExact ? 7.f : kSmoothStraightAngle;
    if (folded <= straightAngle) {
        return fitsPaintBoundary(shape, permitted, width, height, gridExact);
    }
    return fitsPaintNear(shape, permitted, nearMask, width, height, gridExact);
}

// circles float on top: never under another color.
bool coversBlocked(
    Primitive const& shape,
    int sourceWidth,
    int sourceHeight,
    std::vector<std::uint8_t> const& blocked
) {
    if (blocked.size() != static_cast<std::size_t>(sourceWidth) * sourceHeight) return false;
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, sourceWidth, sourceHeight);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            if (!blocked[static_cast<std::size_t>(y) * sourceWidth + x]) continue;
            for (float dy : kCenterOffsets) {
                for (float dx : kCenterOffsets) {
                    if (placed.contains(x + dx, y + dy)) return true;
                }
            }
        }
    }
    return false;
}

struct Contour {
    std::vector<Point> points;
    bool closed = true;
};

std::vector<Contour> traceContours(Region const& region) {
    struct Edge {
        int from = 0;
        int to = 0;
        int dx = 0;
        int dy = 0;
    };

    int const stride = region.width + 1;
    std::vector<Edge> edges;
    std::vector<std::array<int, 2>> outgoing(
        static_cast<std::size_t>(stride) * (region.height + 1), std::array<int, 2>{-1, -1});

    auto add = [&](int x0, int y0, int x1, int y1) {
        int const from = y0 * stride + x0;
        auto& slots = outgoing[static_cast<std::size_t>(from)];
        int const slot = slots[0] < 0 ? 0 : 1;
        if (slot == 1 && slots[1] >= 0) return;
        slots[static_cast<std::size_t>(slot)] = static_cast<int>(edges.size());
        edges.push_back({from, y1 * stride + x1, x1 - x0, y1 - y0});
    };

    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            if (!region.filled(x, y)) continue;
            if (!region.filled(x + 1, y)) add(x + 1, y, x + 1, y + 1);
            if (!region.filled(x, y + 1)) add(x + 1, y + 1, x, y + 1);
            if (!region.filled(x - 1, y)) add(x, y + 1, x, y);
            if (!region.filled(x, y - 1)) add(x, y, x + 1, y);
        }
    }

    auto cornerPoint = [&](int corner) {
        return Point{
            static_cast<float>(corner % stride),
            static_cast<float>(corner / stride)
        };
    };

    std::vector<std::uint8_t> used(edges.size(), 0);
    std::vector<Contour> contours;
    for (std::size_t start = 0; start < edges.size(); ++start) {
        if (used[start]) continue;
        Contour contour;
        int const origin = edges[start].from;
        int last = origin;
        int current = static_cast<int>(start);
        while (current >= 0 && !used[static_cast<std::size_t>(current)]) {
            used[static_cast<std::size_t>(current)] = 1;
            auto const& edge = edges[static_cast<std::size_t>(current)];
            contour.points.push_back(cornerPoint(edge.from));
            last = edge.to;
            int next = -1;
            int bestTurn = 2;
            for (int candidate : outgoing[static_cast<std::size_t>(edge.to)]) {
                if (candidate < 0 || used[static_cast<std::size_t>(candidate)]) continue;
                auto const& option = edges[static_cast<std::size_t>(candidate)];
                int const turn = edge.dx * option.dy - edge.dy * option.dx;
                if (turn < bestTurn) {
                    bestTurn = turn;
                    next = candidate;
                }
            }
            current = next;
        }
        contour.closed = last == origin;
        if (!contour.closed) contour.points.push_back(cornerPoint(last));
        if (contour.points.size() >= 3) contours.push_back(std::move(contour));
    }
    return contours;
}


Contour refineContour(Contour const& contour, int iterations, float tolerance) {
    return {
        smoothAndSimplify(contour.points, contour.closed, iterations, tolerance),
        contour.closed
    };
}

float directionDot(Point const& incoming, Point const& outgoing) {
    return std::clamp(incoming.x * outgoing.x + incoming.y * outgoing.y, -1.f, 1.f);
}

// short cap on silhouettes: the spike would land on the neighbor color.
//
// strokes end cleanly with rectangular ends, as drawn in GD art.


struct Segment {
    Point direction;
    float length = 0.f;
};

// thickness under the point: walks inward out of the blob.
float inwardThickness(
    Region const& region,
    float x,
    float y,
    float inwardX,
    float inwardY,
    float limit,
    bool gridExact = true
) {
    constexpr float kStep = 0.25f;
    // smooth mode starts at the first painted sample (max 1 cell).
    if (!gridExact) {
        float skipped = 0.f;
        while (skipped < 1.f && !region.filledAt(x, y)) {
            x += inwardX * kStep;
            y += inwardY * kStep;
            skipped += kStep;
        }
        if (!region.filledAt(x, y)) return 0.f;
    }
    float depth = 0.f;
    while (depth < limit &&
           region.filledAt(
               x + inwardX * (depth + kStep * 0.5f),
               y + inwardY * (depth + kStep * 0.5f))) {
        depth += kStep;
    }
    return depth;
}

std::vector<Segment> measure(std::vector<Point> const& points, std::size_t segments) {
    std::vector<Segment> output(segments);
    for (std::size_t i = 0; i < segments; ++i) {
        auto const& first = points[i];
        auto const& second = points[(i + 1) % points.size()];
        float const dx = second.x - first.x;
        float const dy = second.y - first.y;
        float const length = std::hypot(dx, dy);
        output[i] = length > 0.001f
            ? Segment{{dx / length, dy / length}, length}
            : Segment{{1.f, 0.f}, 0.f};
    }
    return output;
}

// thickness median: how far to trim when simplifying.
float contourThickness(Region const& region, Contour const& contour, float limit,
                         bool gridExact = true) {
    auto const& loop = contour.points;
    std::size_t const segments = contour.closed ? loop.size() : loop.size() - 1;
    if (segments == 0) return limit;
    auto const measured = measure(loop, segments);
    std::vector<float> depths;
    depths.reserve(segments);
    for (std::size_t i = 0; i < segments; ++i) {
        if (measured[i].length <= 0.05f) continue;
        auto const& first = loop[i];
        auto const& second = loop[(i + 1) % loop.size()];
        float const midX = (first.x + second.x) * 0.5f;
        float const midY = (first.y + second.y) * 0.5f;
        float inwardX = -measured[i].direction.y;
        float inwardY = measured[i].direction.x;
        if (!region.filledAt(midX + inwardX * 0.75f, midY + inwardY * 0.75f)) {
            inwardX = -inwardX;
            inwardY = -inwardY;
        }
        depths.push_back(
            inwardThickness(region, midX, midY, inwardX, inwardY, limit, gridExact));
    }
    if (depths.empty()) return limit;
    auto const middle = depths.begin() + static_cast<std::ptrdiff_t>(depths.size() / 2);
    std::nth_element(depths.begin(), middle, depths.end());
    return std::max(*middle, 1.f);
}

void appendBand(
    std::vector<Primitive>& output,
    Region const& region,
    Contour const& contour,
    float band,
    int color,
    int layer,
    int sourceWidth,
    int sourceHeight,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& permitted,
    bool gridExact = true
) {
    auto const& loop = contour.points;
    if (loop.size() < 2) return;
    std::size_t const segments = contour.closed ? loop.size() : loop.size() - 1;
    auto const measured = measure(loop, segments);

    // overshoot pays seam against color, not against void.
    bool const hasBlocked =
        blocked.size() == static_cast<std::size_t>(sourceWidth) * sourceHeight;
    auto coveredOutside = [&](float x, float y) {
        int const cellX = static_cast<int>(std::floor(x)) + region.offsetX;
        int const cellY = static_cast<int>(std::floor(y)) + region.offsetY;
        if (cellX < 0 || cellY < 0 || cellX >= sourceWidth || cellY >= sourceHeight) {
            return true;
        }
        if (!hasBlocked) return false;
        return blocked[static_cast<std::size_t>(cellY) * sourceWidth + cellX] != 0;
    };
    // excess only counts when it covers the whole strip length.
    auto coveredAlong = [&](Point const& from, Point const& to, float inwardX, float inwardY) {
        int const steps = std::max(
            2, static_cast<int>(std::ceil(pointDistance(from, to))) + 1);
        for (int step = 0; step <= steps; ++step) {
            float const ratio = static_cast<float>(step) / steps;
            float const x = from.x + (to.x - from.x) * ratio - inwardX * 0.5f;
            float const y = from.y + (to.y - from.y) * ratio - inwardY * 0.5f;
            if (!coveredOutside(x, y)) return false;
        }
        return true;
    };

    struct BandGeom {
        float thickness = 1.f;
        float inwardX = 0.f;
        float inwardY = 0.f;
        float offset = 0.f;
    };
    std::vector<BandGeom> geoms(segments);
    std::vector<std::uint8_t> geomValid(segments, 0);
    for (std::size_t i = 0; i < segments; ++i) {
        auto const& segment = measured[i];
        if (segment.length <= 0.05f) continue;
        auto const& first = loop[i];
        auto const& second = loop[(i + 1) % loop.size()];
        float const midX = (first.x + second.x) * 0.5f;
        float const midY = (first.y + second.y) * 0.5f;
        float inwardX = -segment.direction.y;
        float inwardY = segment.direction.x;
        if (!region.filledAt(midX + inwardX * 0.75f, midY + inwardY * 0.75f)) {
            inwardX = -inwardX;
            inwardY = -inwardY;
        }
        // local thickness: the global max fattened the stroke and painted it twice.
        float const rawThick =
            inwardThickness(region, midX, midY, inwardX, inwardY, band, gridExact);
        float const thickness = std::max(rawThick, 1.f);
        float const overshoot = std::min(
            coveredAlong(first, second, inwardX, inwardY)
                ? kOvershoot : kFreeOvershoot,
            thickness * 0.35f);
        geoms[i] = {thickness, inwardX, inwardY, thickness * 0.5f - overshoot};
        geomValid[i] = 1;
    }

    for (std::size_t i = 0; i < segments; ++i) {
        auto const& segment = measured[i];
        if (segment.length <= 0.05f) continue;
        auto const& first = loop[i];
        auto const& second = loop[(i + 1) % loop.size()];
        float const midX = (first.x + second.x) * 0.5f;
        float const midY = (first.y + second.y) * 0.5f;
        float const inwardX = geoms[i].inwardX;
        float const inwardY = geoms[i].inwardY;
        float const thickness = geoms[i].thickness;

        // smooth symmetrical overlap: consecutive segments overlap by
        // ~20-30% of thickness without miter protrusion or spikes.
        float const safeExtension = std::min(thickness * 0.25f, 0.35f);
        float const offset = geoms[i].offset;
        output.push_back({
            midX + inwardX * offset + static_cast<float>(region.offsetX),
            midY + inwardY * offset + static_cast<float>(region.offsetY),
            segment.length + safeExtension * 2.f,
            thickness,
            std::atan2(segment.direction.y, segment.direction.x) * 180.f / kPi,
            static_cast<std::uint16_t>(color),
            PrimitiveKind::Stroke,
            static_cast<std::int16_t>(layer)
        });
        // turn the cap toward the next ribbon using cocos' independent
        // axes. keep its normal thickness; accept only contained caps.
        std::size_t const next = (i + 1) % segments;
        if (!gridExact && contour.closed && geomValid[next] && segment.length >= thickness * 2.f) {
            float const cross = segment.direction.x * measured[next].direction.y -
                segment.direction.y * measured[next].direction.x;
            float const dot = directionDot(segment.direction, measured[next].direction);
            float const skewAngle = std::clamp(std::atan2(cross, dot) * 0.5f,
                -15.f * kPi / 180.f, 15.f * kPi / 180.f);
            if (std::abs(skewAngle) > 0.001f) {
                auto candidate = output.back();
                candidate.skew = skewAngle * 180.f / kPi;
                candidate.height /= std::cos(skewAngle);
                if (fitsPaintBoundary(candidate, permitted, sourceWidth, sourceHeight, false) &&
                    shapeSpill(candidate, permitted, sourceWidth, sourceHeight) <=
                        shapeSpill(output.back(), permitted, sourceWidth, sourceHeight) + 0.001f) {
                    output.back() = candidate;
                }
            }
        }
    }
}

struct StrokeCoverageField {
    int width;
    int height;
    std::span<float const> samples;

    bool valid() const { return samples.size() == static_cast<std::size_t>(width) * height; }

    float at(float px, float py) const {
        float const gx = px - 0.5f, gy = py - 0.5f;
        int const ix = static_cast<int>(std::floor(gx)), iy = static_cast<int>(std::floor(gy));
        float const fx = gx - ix, fy = gy - iy;
        float value = 0.f;
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            int const xx = ix + dx, yy = iy + dy;
            if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
            value += samples[static_cast<std::size_t>(yy) * width + xx] *
                (dx ? fx : 1.f - fx) * (dy ? fy : 1.f - fy);
        }
        return value;
    }
};

struct StrokeWidthSampler {
    Region const& region;
    StrokeCoverageField const& source;
    float reach;

    float halfWidth(Point origin, Point direction) const {
        if (source.valid()) {
            float sum = 0.f;
            int empty = 0;
            for (float distance = 0.125f; distance < reach; distance += 0.25f) {
                float const alpha = source.at(origin.x + direction.x * distance, origin.y + direction.y * distance);
                sum += alpha * 0.25f;
                empty = alpha < 0.025f ? empty + 1 : 0;
                if (empty >= 3) break;
            }
            return sum;
        }
        float distance = 0.f;
        while (distance < reach) {
            distance += 0.125f;
            if (!region.filledAt(origin.x + direction.x * distance - region.offsetX,
                                 origin.y + direction.y * distance - region.offsetY))
                return std::max(0.f, distance - 0.0625f);
        }
        return distance;
    }

    float thickness(Primitive const& body) const {
        float const angle = body.rotation * kPi / 180.f;
        float const cosine = std::cos(angle), sine = std::sin(angle);
        float sum = 0.f;
        for (float along : {-0.25f, 0.f, 0.25f}) {
            Point const point{body.x + along * body.width * cosine, body.y + along * body.width * sine};
            sum += halfWidth(point, {-sine, cosine}) + halfWidth(point, {sine, -cosine});
        }
        return std::max(0.5f, sum / 3.f);
    }
};

std::vector<Point> smoothStrokeAxis(std::vector<Point> const& centered, bool closed) {
    if (centered.size() < 11) return centered;
    // quadratic smoothing removes pixel-sized axis oscillations while
    // keeping the curvature of a real stroke.
    constexpr std::array weights{-36.f, 9.f, 44.f, 69.f, 84.f, 89.f,
                                 84.f, 69.f, 44.f, 9.f, -36.f};
    int const count = static_cast<int>(centered.size()) - (closed ? 1 : 0);
    auto filtered = centered;
    auto at = [&](int index) {
        if (closed) return centered[(index % count + count) % count];
        if (index < 0) return Point{2.f * centered.front().x - centered[-index].x,
                                  2.f * centered.front().y - centered[-index].y};
        if (index >= count) return Point{2.f * centered.back().x - centered[2 * count - 2 - index].x,
                                        2.f * centered.back().y - centered[2 * count - 2 - index].y};
        return centered[index];
    };
    for (int index = 0; index < count; ++index) {
        Point sum;
        for (int offset = -5; offset <= 5; ++offset) {
            auto const point = at(index + offset);
            sum.x += point.x * weights[offset + 5];
            sum.y += point.y * weights[offset + 5];
        }
        filtered[index] = {sum.x / 429.f, sum.y / 429.f};
    }
    if (closed) filtered.back() = filtered.front();
    return filtered;
}

// without a stroke it draws nothing: the caller uses the outline path.
bool appendChain(
    std::vector<Primitive>& output,
    Region const& region,
    std::vector<int> const& component,
    int sourceWidth,
    int sourceHeight,
    float radius,
    int color,
    int layer,
    std::vector<std::uint8_t> const& /*blocked*/,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true,
    float centerlineTolerance = 0.f
) {
    auto const skeleton = thin(component, sourceWidth);
    auto const paths = skeletonPaths(skeleton, centerlineTolerance > 0.f);
    // axis capped at half thickness, else the strip crosses the drawing.
    float const tolerance = centerlineTolerance > 0.f ? centerlineTolerance : gridExact
        ? std::clamp(radius * 0.5f, 0.6f, 1.3f)
        : std::clamp(radius * 0.25f, 0.25f, 0.6f);

    struct Line {
        std::vector<Point> points;
        std::array<bool, 2> joined{};
        std::array<bool, 2> terminal{};
        // -1 = unknown turn: half-thickness square cap.
        std::array<float, 2> jointDot{{-1.f, -1.f}};
    };
    std::vector<Line> lines;
    auto degree = [&](int position) {
        int count = 0;
        int const x = position % skeleton.width;
        int const y = position / skeleton.width;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int const xx = x + dx;
                int const yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= skeleton.width || yy >= skeleton.height) continue;
                if (centerlineTolerance > 0.f && dx != 0 && dy != 0 &&
                    (skeleton.cells[static_cast<std::size_t>(y) * skeleton.width + xx] ||
                     skeleton.cells[static_cast<std::size_t>(yy) * skeleton.width + x])) continue;
                count += skeleton.cells[static_cast<std::size_t>(yy) * skeleton.width + xx] != 0;
            }
        }
        return count;
    };
    for (auto const& path : paths) {
        std::vector<Point> points;
        points.reserve(path.size());
        for (int position : path) {
            points.push_back({
                static_cast<float>(position % skeleton.width + skeleton.offsetX) + 0.5f,
                static_cast<float>(position / skeleton.width + skeleton.offsetY) + 0.5f
            });
        }
        auto raycast = [&](Point start, Point dir) -> float {
            float dist = 0.f;
            while (dist < radius * 2.5f) {
                dist += 0.25f;
                float const px = start.x + dir.x * dist - static_cast<float>(region.offsetX);
                float const py = start.y + dir.y * dist - static_cast<float>(region.offsetY);
                int const ix = static_cast<int>(std::floor(px));
                int const iy = static_cast<int>(std::floor(py));
                if (ix < 0 || iy < 0 || ix >= region.width || iy >= region.height ||
                    !region.cells[static_cast<std::size_t>(iy) * region.width + ix]) {
                    return dist - 0.125f;
                }
            }
            return dist;
        };

        bool const closed = !gridExact && points.size() > 3 &&
            pointDistance(points.front(), points.back()) < 0.01f;
        std::vector<Point> centered = points;
        for (std::size_t i = 0; i < points.size(); ++i) {
            bool const seam = closed && (i == 0 || i + 1 == points.size());
            Point prev = seam ? points[points.size() - 2]
                : (i > 0) ? points[i - 1] : points[i];
            Point next = seam ? points[1]
                : (i + 1 < points.size()) ? points[i + 1] : points[i];
            if (centerlineTolerance > 0.f) {
                // a one-pixel tangent alternates on staircases; use a wider
                // neighborhood before measuring the normal and width.
                int const count = static_cast<int>(points.size()) - (closed ? 1 : 0);
                auto at = [&](int index) {
                    return points[closed ? (index % count + count) % count : std::clamp(index, 0, count - 1)];
                };
                prev = at(static_cast<int>(i) - 3);
                next = at(static_cast<int>(i) + 3);
            }
            float const tx = next.x - prev.x;
            float const ty = next.y - prev.y;
            float const len = std::hypot(tx, ty);
            if (len < 0.001f) continue;
            Point const normal{-ty / len, tx / len};
            float const dPlus = raycast(points[i], normal);
            float const dMinus = raycast(points[i], {-normal.x, -normal.y});
            float const shift = (dPlus - dMinus) * 0.5f;
            centered[i].x = points[i].x + normal.x * shift;
            centered[i].y = points[i].y + normal.y * shift;
        }

        if (centerlineTolerance > 0.f) centered = smoothStrokeAxis(centered, closed);
        std::vector<Point> reduced;
        if (closed) {
            centered.pop_back();
            reduced = smoothAndSimplify(centered, true, 2, tolerance);
            if (reduced.size() >= 3) reduced.push_back(reduced.front());
        } else {
            reduced = simplify(gridExact ? centered : smoothPath(centered, 2), tolerance);
        }
        if (reduced.size() < 2) continue;
        float length = 0.f;
        for (std::size_t i = 1; i < reduced.size(); ++i) {
            length += pointDistance(reduced[i - 1], reduced[i]);
        }
        if (length < std::max(1.5f, radius * 0.8f)) continue;
        lines.push_back({
            std::move(reduced), {},
            {degree(path.front()) <= 1, degree(path.back()) <= 1}
        });
    }
    if (lines.empty()) return false;
    auto nearMask = nearCells(component, sourceWidth, sourceHeight);
    // smooth relaxes void; pixel never forgives.
    if (gridExact) maskVoid(nearMask, empty);

    for (std::size_t first = 0; first < lines.size(); ++first) {
        for (std::size_t second = first + 1; second < lines.size(); ++second) {
            for (int firstEnd = 0; firstEnd < 2; ++firstEnd) {
                for (int secondEnd = 0; secondEnd < 2; ++secondEnd) {
                    auto& firstPoints = lines[first].points;
                    auto& secondPoints = lines[second].points;
                    std::size_t const firstIndex = firstEnd ? firstPoints.size() - 1 : 0;
                    std::size_t const secondIndex = secondEnd ? secondPoints.size() - 1 : 0;
                    Point const firstPoint = firstPoints[firstIndex];
                    Point const secondPoint = secondPoints[secondIndex];
                    if (pointDistance(firstPoint, secondPoint) > radius) continue;
                    Point const firstNear = firstPoints[firstEnd ? firstIndex - 1 : 1];
                    Point const secondNear = secondPoints[secondEnd ? secondIndex - 1 : 1];
                    Point const firstDirection{
                        firstPoint.x - firstNear.x, firstPoint.y - firstNear.y
                    };
                    Point const secondDirection{
                        secondPoint.x - secondNear.x, secondPoint.y - secondNear.y
                    };
                    float const determinant = firstDirection.x * secondDirection.y -
                        firstDirection.y * secondDirection.x;
                    if (std::abs(determinant) < 0.05f) continue;
                    float const dx = secondPoint.x - firstPoint.x;
                    float const dy = secondPoint.y - firstPoint.y;
                    float const distance =
                        (dx * secondDirection.y - dy * secondDirection.x) / determinant;
                    Point const intersection{
                        firstPoint.x + firstDirection.x * distance,
                        firstPoint.y + firstDirection.y * distance
                    };
                    if (pointDistance(firstPoint, intersection) > radius ||
                        pointDistance(secondPoint, intersection) > radius) {
                        continue;
                    }
                    float const firstLength = std::hypot(firstDirection.x, firstDirection.y);
                    float const secondLength = std::hypot(
                        secondDirection.x, secondDirection.y);
                    float const jointDot = firstLength > 0.001f && secondLength > 0.001f
                        ? directionDot(
                              {firstDirection.x / firstLength, firstDirection.y / firstLength},
                              {-secondDirection.x / secondLength,
                               -secondDirection.y / secondLength})
                        : -1.f;
                    firstPoints[firstIndex] = intersection;
                    secondPoints[secondIndex] = intersection;
                    lines[first].joined[static_cast<std::size_t>(firstEnd)] = true;
                    lines[second].joined[static_cast<std::size_t>(secondEnd)] = true;
                    lines[first].jointDot[static_cast<std::size_t>(firstEnd)] = jointDot;
                    lines[second].jointDot[static_cast<std::size_t>(secondEnd)] = jointDot;
                }
            }
        }
    }

    for (auto& line : lines) {
        for (int end = 0; end < 2; ++end) {
            if (!line.terminal[static_cast<std::size_t>(end)]) {
                line.joined[static_cast<std::size_t>(end)] = true;
            }
        }
    }

    for (auto& line : lines) {
        for (int end = 0; end < 2; ++end) {
            if (line.joined[static_cast<std::size_t>(end)]) continue;
            std::size_t const index = end ? line.points.size() - 1 : 0;
            Point const neighbor = line.points[end ? index - 1 : 1];
            Point& point = line.points[index];
            float const dx = point.x - neighbor.x;
            float const dy = point.y - neighbor.y;
            float const length = std::hypot(dx, dy);
            if (length <= 0.01f) continue;
            float const directionX = dx / length;
            float const directionY = dy / length;
            float const extension = 0.5f * (
                std::abs(directionX) + std::abs(directionY));
            point.x += directionX * extension;
            point.y += directionY * extension;
        }
    }

    double span = 0.0;
    for (auto const& line : lines) {
        for (std::size_t i = 1; i < line.points.size(); ++i) {
            span += pointDistance(line.points[i - 1], line.points[i]);
        }
    }

    float const nominal = std::clamp(
        static_cast<float>(static_cast<double>(component.size()) / std::max(span, 0.001)),
        0.8f, radius * 2.4f);

    // short axis = compact blob: outline path, not strip.
    if (span < nominal * kChainSlenderness) return false;

    // strips aside: the axis may shortcut through colorless ground.
    std::vector<Primitive> strokes;



    // prior thickness, deciding which elbows get discs.
    std::vector<std::vector<float>> thicknessOf(lines.size());
    for (std::size_t slot = 0; slot < lines.size(); ++slot) {
        auto const& reduced = lines[slot].points;
        std::size_t const segments = reduced.size() - 1;
        thicknessOf[slot].assign(segments, 1.f);
        for (std::size_t i = 0; i < segments; ++i) {
            auto const& first = reduced[i];
            auto const& second = reduced[i + 1];
            float const midX = (first.x + second.x) * 0.5f;
            float const midY = (first.y + second.y) * 0.5f;
            float const local = region.distanceAt(
                midX - static_cast<float>(region.offsetX),
                midY - static_cast<float>(region.offsetY));
            float const thicknessScale = radius <= 1.5f ? 1.f : 0.95f;
            float const minimumThickness = radius <= 1.5f ? 0.9f : 0.8f;
            thicknessOf[slot][i] = std::clamp(
                local * 2.f, nominal * 0.8f,
                std::max(nominal * thicknessScale, minimumThickness));
        }
    }

    for (std::size_t slot = 0; slot < lines.size(); ++slot) {
        auto const& line = lines[slot];
        auto const& reduced = line.points;
        std::size_t const segments = reduced.size() - 1;
        auto const measured = measure(reduced, segments);
        for (std::size_t i = 0; i < segments; ++i) {
            auto const& segment = measured[i];
            if (segment.length <= 0.05f) continue;
            auto const& first = reduced[i];
            auto const& second = reduced[i + 1];
            float const midX = (first.x + second.x) * 0.5f;
            float const midY = (first.y + second.y) * 0.5f;
            float const thickness = thicknessOf[slot][i];

            // smooth symmetrical overlap: adjacent segments overlap by
            // ~20-30% of thickness without miter spikes or joint circles.
            float const overlap = std::min(thickness * 0.25f, 0.35f);
            strokes.push_back({
                midX,
                midY,
                segment.length + overlap * 2.f,
                thickness,
                std::atan2(segment.direction.y, segment.direction.x) * 180.f / kPi,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Stroke,
                static_cast<std::int16_t>(layer)
            });
        }
    }
    if (strokes.empty()) return false;
    // trim thickness slightly when spilling so curves stay connected without holes.
    if (!gridExact) {
        for (auto& stroke : strokes) {
            if (shapeFarSpill(stroke, permitted, nearMask, sourceWidth, sourceHeight) > kChainEraseSpill) {
                stroke.height = std::max(0.35f, stroke.height * 0.75f);
            }
        }
    }
    output.insert(output.end(), strokes.begin(), strokes.end());
    return true;
}

// solid round regions can use one native ellipse. boundary ribbons and fills
// stay as strips; an ellipse is accepted only when its cell coverage fits.
bool appendCircle(
    std::vector<Primitive>& output,
    Region const& region,
    int color,
    int layer,
    int sourceWidth,
    int sourceHeight,
    std::vector<std::uint8_t> const& blocked
) {
    float const width = static_cast<float>(region.width - kPadding * 2);
    float const height = static_cast<float>(region.height - kPadding * 2);
    if (std::min(width, height) < 3.f || std::max(width, height) > std::min(width, height) * 3.f) return false;
    Primitive candidate{region.offsetX + region.width * 0.5f,
        region.offsetY + region.height * 0.5f, width, height, 0.f,
        static_cast<std::uint16_t>(color), PrimitiveKind::Circle, static_cast<std::int16_t>(layer)};
    auto matches = [&](Primitive const& ellipse) {
        auto const shape = xformOf(ellipse);
        for (int y = 0; y < region.height; ++y) for (int x = 0; x < region.width; ++x) {
            int const wx = x + region.offsetX, wy = y + region.offsetY;
            if (wx < 0 || wy < 0 || wx >= sourceWidth || wy >= sourceHeight) continue;
            bool const paints = shape.contains(wx + 0.5f, wy + 0.5f);
            bool const coveredLater = blocked.size() == static_cast<std::size_t>(sourceWidth) * sourceHeight &&
                blocked[static_cast<std::size_t>(wy) * sourceWidth + wx];
            if (region.filled(x, y) ? !paints : (paints && !coveredLater)) return false;
        }
        return true;
    };
    if (matches(candidate)) { output.push_back(candidate); return true; }
    auto const filled = std::count(region.cells.begin(), region.cells.end(), std::uint8_t{1});
    float const area = width * height * kPi * 0.25f;
    int covered = 0;
    if (blocked.size() == static_cast<std::size_t>(sourceWidth) * sourceHeight) {
        for (int y = kPadding; y < region.height - kPadding; ++y)
            for (int x = kPadding; x < region.width - kPadding; ++x)
                covered += blocked[static_cast<std::size_t>(y + region.offsetY) * sourceWidth + x + region.offsetX] != 0;
    }
    if (std::max(width, height) > 32.f || filled < area * 0.5f ||
        filled + covered < area * 0.85f || filled > area * 1.15f) return false;
    constexpr std::array offsets{0.f, -0.25f, 0.25f, -0.125f, 0.125f, -0.5f, 0.5f};
    for (float dx : offsets) for (float dy : offsets) for (float dw : {0.f, -0.25f, 0.25f, 0.5f, 1.f, 2.f})
        for (float dh : {0.f, -0.25f, 0.25f, 0.5f, 1.f, 2.f}) {
            if (!covered && (std::abs(dx) > 0.375f || std::abs(dy) > 0.375f || dw > 0.25f || dh > 0.25f)) continue;
            auto adjusted = candidate;
            adjusted.x += dx; adjusted.y += dy; adjusted.width += dw; adjusted.height += dh;
            if (matches(adjusted)) { output.push_back(adjusted); return true; }
        }
    return false;
}






bool appendCapsule(
    std::vector<Primitive>&,
    std::vector<int> const&,
    int,
    int,
    int,
    int,
    std::vector<std::uint8_t> const&,
    std::vector<std::uint8_t> const&
) {
    return false;
}


bool appendTriangle(
    std::vector<Primitive>& output,
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& blocked
) {
    if (positions.size() < 8) return false;
    std::vector<Point> points;
    points.reserve(positions.size());
    for (int position : positions) points.push_back({position % width + 0.5f, position / width + 0.5f});
    std::sort(points.begin(), points.end(), [](Point a, Point b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    auto cross = [](Point a, Point b, Point c) {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    };
    std::vector<Point> hull;
    for (auto point : points) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), point) <= 0.f) hull.pop_back();
        hull.push_back(point);
    }
    auto const lower = hull.size();
    for (std::size_t index = points.size() - 1; index-- > 0;) {
        auto const point = points[index];
        while (hull.size() > lower && cross(hull[hull.size() - 2], hull.back(), point) <= 0.f) hull.pop_back();
        hull.push_back(point);
    }
    if (hull.size() < 4) return false;
    hull.pop_back();
    struct Edge { Point from; Point to; float length; std::size_t index; };
    std::vector<Edge> edges;
    for (std::size_t index = 0; index < hull.size(); ++index) {
        auto const a = hull[index], b = hull[(index + 1) % hull.size()];
        edges.push_back({a, b, pointDistance(a, b), index});
    }
    std::sort(edges.begin(), edges.end(), [](auto const& a, auto const& b) { return a.length > b.length; });
    std::vector<Edge> sides;
    for (auto const& edge : edges) {
        if (edge.length < 1.f) continue;
        bool distinct = true;
        for (auto const& side : sides) {
            float const dot = ((edge.to.x - edge.from.x) * (side.to.x - side.from.x) +
                (edge.to.y - edge.from.y) * (side.to.y - side.from.y)) / (edge.length * side.length);
            if (std::abs(dot) > 0.94f) distinct = false;
        }
        if (distinct) sides.push_back(edge);
        if (sides.size() == 3) break;
    }
    if (sides.size() != 3) return false;
    std::sort(sides.begin(), sides.end(), [](auto const& a, auto const& b) { return a.index < b.index; });
    std::vector<std::uint8_t> permitted(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) permitted[static_cast<std::size_t>(position)] = 1;
    if (blocked.size() == permitted.size())
        for (std::size_t index = 0; index < permitted.size(); ++index) permitted[index] |= blocked[index];
    for (float outset : {0.001f, 0.025f, 0.05f, 0.1f, 0.15f, 0.25f}) {
        std::array<Point, 3> corners{};
        bool valid = true;
        for (std::size_t index = 0; index < 3; ++index) {
            auto const a = sides[index], b = sides[(index + 1) % 3];
            Point const ad{a.to.x - a.from.x, a.to.y - a.from.y};
            Point const bd{b.to.x - b.from.x, b.to.y - b.from.y};
            Point const ap{a.from.x + ad.y / a.length * outset, a.from.y - ad.x / a.length * outset};
            Point const bp{b.from.x + bd.y / b.length * outset, b.from.y - bd.x / b.length * outset};
            float const determinant = ad.x * bd.y - ad.y * bd.x;
            if (std::abs(determinant) < 0.001f) { valid = false; break; }
            float const along = ((bp.x - ap.x) * bd.y - (bp.y - ap.y) * bd.x) / determinant;
            corners[index] = {ap.x + along * ad.x, ap.y + along * ad.y};
        }
        if (!valid) continue;
        auto a = corners[0], b = corners[1], c = corners[2];
        float const area = std::abs(cross(a, b, c)) * 0.5f;
        if (area < positions.size() * 0.9f || area > positions.size() * 1.15f) continue;
        if (cross(a, b, c) < 0.f) std::swap(b, c);
        float const xAngle = std::atan2(b.y - a.y, b.x - a.x);
        float const yAngle = std::atan2(c.y - a.y, c.x - a.x);
        Primitive candidate{(b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f,
            pointDistance(a, b), pointDistance(a, c), xAngle * 180.f / kPi,
            static_cast<std::uint16_t>(color), PrimitiveKind::Triangle, static_cast<std::int16_t>(layer)};
        candidate.skew = (yAngle - xAngle) * 180.f / kPi - 90.f;
        auto const shape = xformOf(candidate);
        for (int position : positions)
            if (!shape.contains(position % width + 0.5f, position / width + 0.5f)) { valid = false; break; }
        if (!valid) continue;
        auto const box = xformBox(shape, width, height);
        std::size_t foreignCenters = 0;
        for (int y = box[1]; y <= box[3] && valid; ++y) for (int x = box[0]; x <= box[2]; ++x)
            if (!permitted[static_cast<std::size_t>(y) * width + x] && shape.contains(x + 0.5f, y + 0.5f)) {
                if (++foreignCenters > positions.size() / 100) { valid = false; break; }
            }
        if (!valid || shapeSpill(candidate, permitted, width, height) > 0.06f) continue;
        output.push_back(candidate);
        return true;
    }
    return false;
}


std::vector<std::uint8_t> insideContours(
    Region const& region,
    std::vector<Contour> const& contours
) {
    std::vector<std::uint8_t> inside(
        static_cast<std::size_t>(region.width) * region.height, 0);
    std::vector<float> crossings;
    for (int y = 0; y < region.height; ++y) {
        float const sample = static_cast<float>(y) + 0.5f;
        crossings.clear();
        for (auto const& contour : contours) {
            auto const& points = contour.points;
            if (points.size() < 3) continue;
            for (std::size_t i = 0; i < points.size(); ++i) {
                auto const& first = points[i];
                auto const& second = points[(i + 1) % points.size()];
                if ((first.y <= sample) == (second.y <= sample)) continue;
                float const ratio = (sample - first.y) / (second.y - first.y);
                crossings.push_back(first.x + ratio * (second.x - first.x));
            }
        }
        std::sort(crossings.begin(), crossings.end());
        for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
            int const from = std::max(
                0, static_cast<int>(std::ceil(crossings[i] - 0.5f)));
            int const to = std::min(
                region.width - 1, static_cast<int>(std::floor(crossings[i + 1] - 0.5f)));
            for (int x = from; x <= to; ++x) {
                inside[static_cast<std::size_t>(y) * region.width + x] = 1;
            }
        }
    }
    return inside;
}

std::vector<std::uint8_t> coverageMask(
    Region const& region,
    std::vector<Primitive> const& objects,
    bool whole,
    bool gridExact = true
) {
    constexpr std::array<Point, 16> kWholeSamples{
        Point{0.2f, 0.2f}, Point{0.4f, 0.2f}, Point{0.6f, 0.2f}, Point{0.8f, 0.2f},
        Point{0.2f, 0.4f}, Point{0.4f, 0.4f}, Point{0.6f, 0.4f}, Point{0.8f, 0.4f},
        Point{0.2f, 0.6f}, Point{0.4f, 0.6f}, Point{0.6f, 0.6f}, Point{0.8f, 0.6f},
        Point{0.2f, 0.8f}, Point{0.4f, 0.8f}, Point{0.6f, 0.8f}, Point{0.8f, 0.8f}
    };
    std::size_t const sampleCount = whole ? kWholeSamples.size() : 1;
    std::vector<std::uint16_t> samples(
        static_cast<std::size_t>(region.width) * region.height, 0);
    auto const full = static_cast<std::uint16_t>((1u << sampleCount) - 1u);
    for (auto const& object : objects) {
        auto const placed = xformOf(object);
        int const minX = std::max(0, static_cast<int>(
            std::floor(placed.x - placed.extentX)) - region.offsetX);
        int const minY = std::max(0, static_cast<int>(
            std::floor(placed.y - placed.extentY)) - region.offsetY);
        int const maxX = std::min(region.width - 1, static_cast<int>(
            std::ceil(placed.x + placed.extentX)) - region.offsetX);
        int const maxY = std::min(region.height - 1, static_cast<int>(
            std::ceil(placed.y + placed.extentY)) - region.offsetY);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
                if (samples[index] == full) continue;
                for (std::size_t sample = 0; sample < sampleCount; ++sample) {
                    Point const point = whole ? kWholeSamples[sample] : Point{0.5f, 0.5f};
                    if (!placed.contains(
                            static_cast<float>(x + region.offsetX) + point.x,
                            static_cast<float>(y + region.offsetY) + point.y)) {
                        continue;
                    }
                    samples[index] |= static_cast<std::uint16_t>(1u << sample);
                }
            }
        }
    }
    std::vector<std::uint8_t> covered(samples.size(), 0);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (!whole || gridExact) {
            covered[i] = samples[i] == full;
            continue;
        }
        // smooth: half a cell suffices; 16/16 left loose blocks on diagonals.
        covered[i] = std::popcount(samples[i]) >= 8;
    }
    return covered;
}

std::vector<int> selectCells(
    Region const& region,
    int sourceWidth,
    std::vector<std::uint8_t> const& covered,
    std::vector<std::uint8_t> const& inside,
    bool requireMask,
    float minimumDepth
) {
    std::vector<int> positions;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
            if (covered[index]) continue;
            if (!inside.empty() && !inside[index]) continue;
            if (requireMask && !region.cells[index]) continue;
            if (region.distance[index] < minimumDepth) continue;
            positions.push_back((y + region.offsetY) * sourceWidth + x + region.offsetX);
        }
    }
    return positions;
}

void appendBlocks(
    std::vector<Primitive>& output,
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& spare = {}
) {
    if (positions.empty()) return;
    auto blocks = packBlocks(positions, width, height, color, spare);
    for (auto& block : blocks) {
        block.layer = static_cast<std::int16_t>(layer);
        output.push_back(block);
    }
}

bool appendSmallPatch(
    std::vector<Primitive>& output,
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& /*blocked*/,
    bool gridExact = true,
    std::optional<float> forcedAngle = {}
) {
    if (positions.size() < 2) return false;
    float meanX = 0.f;
    float meanY = 0.f;
    for (int position : positions) {
        meanX += static_cast<float>(position % width) + 0.5f;
        meanY += static_cast<float>(position / width) + 0.5f;
    }
    meanX /= static_cast<float>(positions.size());
    meanY /= static_cast<float>(positions.size());

    float xx = 0.f;
    float xy = 0.f;
    float yy = 0.f;
    for (int position : positions) {
        float const x = static_cast<float>(position % width) + 0.5f - meanX;
        float const y = static_cast<float>(position / width) + 0.5f - meanY;
        xx += x * x;
        xy += x * y;
        yy += y * y;
    }
    float const angle = forcedAngle.value_or(0.5f * std::atan2(2.f * xy, xx - yy));
    float const cosine = std::cos(angle);
    float const sine = std::sin(angle);
    float minMajor = std::numeric_limits<float>::max();
    float maxMajor = std::numeric_limits<float>::lowest();
    float minMinor = std::numeric_limits<float>::max();
    float maxMinor = std::numeric_limits<float>::lowest();
    for (int position : positions) {
        float const x = static_cast<float>(position % width) + 0.5f;
        float const y = static_cast<float>(position / width) + 0.5f;
        float const major = x * cosine + y * sine;
        float const minor = -x * sine + y * cosine;
        minMajor = std::min(minMajor, major);
        maxMajor = std::max(maxMajor, major);
        minMinor = std::min(minMinor, minor);
        maxMinor = std::max(maxMinor, minor);
    }

    float const halfWidth = std::max(
        (maxMajor - minMajor) * 0.5f + 0.5f, kRepairDiameter * 0.5f);
    float const halfHeight = std::max(
        (maxMinor - minMinor) * 0.5f + 0.5f, kRepairDiameter * 0.5f);
    // turned l/s boxes bulldoze voids: cap their spare.
    if (halfWidth * halfHeight * 4.f >
        static_cast<float>(positions.size()) * kPatchSlack) {
        return false;
    }
    float const middleMajor = (minMajor + maxMajor) * 0.5f;
    float const middleMinor = (minMinor + maxMinor) * 0.5f;
    Point const center{
        middleMajor * cosine - middleMinor * sine,
        middleMajor * sine + middleMinor * cosine
    };
    Primitive const patch{
        center.x, center.y, halfWidth * 2.f, halfHeight * 2.f,
        angle * 180.f / kPi, static_cast<std::uint16_t>(color),
        PrimitiveKind::Stroke,
        static_cast<std::int16_t>(layer)
    };

    if (!fitsPaintBoundary(patch, permitted, width, height, gridExact)) return false;
    output.push_back(patch);
    return true;
}

constexpr int kPruneScale = 8;

struct PruneEntry {
    Primitive const* object = nullptr;
    std::uint8_t* keep = nullptr;
};

// a strip with constant vertical thickness has shared vertical end caps. a
// rotated rectangle cannot express those; independent axes can, without a
// separate square at every bend.
bool appendColumnRibbons(std::vector<Primitive>& output, std::vector<int> const& positions,
                         int width, int height, int color, int layer,
                         std::vector<std::uint8_t> const& permitted) {
    if (positions.size() < 2 || width <= 0 || height <= 0) return false;
    auto const box = bounds(positions, width);
    int const columns = box[2] - box[0] + 1;
    std::vector<int> low(columns, height), high(columns, -1), count(columns, 0);
    for (int position : positions) {
        auto const column = static_cast<std::size_t>(position % width - box[0]);
        low[column] = std::min(low[column], position / width);
        high[column] = std::max(high[column], position / width);
        ++count[column];
    }
    int const thickness = high[0] - low[0] + 1;
    if (thickness <= 0 || thickness > 8 || columns < thickness * 3) return false;
    std::vector<Point> centers;
    for (int column = 0; column < columns; ++column) {
        if (count[column] != thickness || high[column] - low[column] + 1 != thickness) return false;
        centers.push_back({box[0] + column + 0.5f, (low[column] + high[column] + 1) * 0.5f});
    }
    auto points = simplify(centers, 0.15f);
    if (points.size() < 2) return false;
    float const firstSlope = (points[1].y - points[0].y) / (points[1].x - points[0].x);
    float const lastSlope = (points.back().y - points[points.size() - 2].y) /
        (points.back().x - points[points.size() - 2].x);
    points.front().x -= 0.5f; points.front().y -= firstSlope * 0.5f;
    points.back().x += 0.5f; points.back().y += lastSlope * 0.5f;
    std::vector<Primitive> ribbons;
    for (std::size_t index = 1; index < points.size(); ++index) {
        auto const a = points[index - 1], b = points[index];
        float const angle = std::atan2(b.y - a.y, b.x - a.x) * 180.f / kPi;
        if (std::abs(angle) > 80.f) return false;
        Primitive ribbon{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f,
            pointDistance(a, b) + 0.01f, static_cast<float>(thickness), angle,
            static_cast<std::uint16_t>(color), PrimitiveKind::Stroke, static_cast<std::int16_t>(layer)};
        ribbon.skew = -angle;
        ribbons.push_back(ribbon);
    }
    auto const forms = xformsOf(ribbons);
    auto covered = [&](float x, float y) {
        return std::any_of(forms.begin(), forms.end(), [&](auto const& shape) { return shape.contains(x, y); });
    };
    for (int position : positions)
        if (!covered(position % width + 0.5f, position / width + 0.5f)) return false;
    for (int y = std::max(0, box[1] - 2); y <= std::min(height - 1, box[3] + 2); ++y)
        for (int x = std::max(0, box[0] - 1); x <= std::min(width - 1, box[2] + 1); ++x)
            if (!permitted[static_cast<std::size_t>(y) * width + x] && covered(x + 0.5f, y + 0.5f)) return false;
    output.insert(output.end(), ribbons.begin(), ribbons.end());
    return true;
}

template <typename Test>
bool anySample(ShapeXform const& shape, int width, int height, Test test) {
    return forEachSample(shape, width, height, kPruneScale, [&](int x, int y) {
        return test(static_cast<std::size_t>(y) * width * kPruneScale + x);
    });
}

template <typename Test>
bool anySample(Primitive const& object, int width, int height, Test test) {
    return anySample(xformOf(object), width, height, test);
}

// drops what never changes the drawing, bottom-up and top-down.
void markUsefulObjects(std::vector<PruneEntry> entries, int width, int height) {
    std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right) {
        return left.object->layer < right.object->layer;
    });
    std::vector<ShapeXform> forms;
    forms.reserve(entries.size());
    for (auto const& entry : entries) forms.push_back(xformOf(*entry.object));
    std::size_t const samples =
        static_cast<std::size_t>(width) * height * kPruneScale * kPruneScale;

    std::vector<std::int16_t> painted(samples, -1);
    std::vector<std::uint8_t> useful(entries.size(), 0);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto const color = static_cast<std::int16_t>(entries[index].object->color);
        bool const changesPaint = anySample(forms[index], width, height, [&](std::size_t sample) {
            return painted[sample] != color;
        });
        // a positive subpixel shape can fall between all audit samples.
        bool const subpixel = std::min(forms[index].width, forms[index].height) < 1.f / kPruneScale;
        if (!changesPaint && !subpixel) continue;
        useful[index] = 1;
        anySample(forms[index], width, height, [&](std::size_t sample) {
            painted[sample] = color;
            return false;
        });
    }

    std::vector<std::uint8_t> covered(samples, 0);
    for (std::size_t index = entries.size(); index-- > 0;) {
        if (!useful[index]) continue;
        bool const subpixel = std::min(forms[index].width, forms[index].height) < 1.f / kPruneScale;
        if (!subpixel && !anySample(forms[index], width, height, [&](std::size_t sample) {
                return covered[sample] == 0;
            })) continue;
        *entries[index].keep = 1;
        anySample(forms[index], width, height, [&](std::size_t sample) {
            covered[sample] = 1;
            return false;
        });
    }
}

void compactKept(std::vector<Primitive>& objects, std::vector<std::uint8_t> const& keep) {
    std::size_t destination = 0;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        if (!keep[index]) continue;
        if (destination != index) objects[destination] = std::move(objects[index]);
        ++destination;
    }
    objects.resize(destination);
}

// adjacent same-color squares merge even across layers.
void mergePaintBlocks(std::vector<Primitive>& objects) {
    struct Box {
        int minX = 0;
        int minY = 0;
        int maxX = 0;
        int maxY = 0;
        std::size_t index = 0;
        bool alive = true;
    };

    std::vector<Box> boxes;
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto const& object = objects[index];
        if (object.kind != PrimitiveKind::Block || object.rotation != 0.f) continue;
        Box box{
            static_cast<int>(std::lround(object.x - object.width * 0.5f)),
            static_cast<int>(std::lround(object.y - object.height * 0.5f)),
            static_cast<int>(std::lround(object.x + object.width * 0.5f)),
            static_cast<int>(std::lround(object.y + object.height * 0.5f)),
            index,
            true
        };
        // grid blocks only: the rest never adds without moving the drawing.
        if (std::abs(static_cast<float>(box.maxX - box.minX) - object.width) > 0.001f ||
            std::abs(static_cast<float>(box.maxY - box.minY) - object.height) > 0.001f) {
            continue;
        }
        boxes.push_back(box);
    }
    if (boxes.size() < 2) return;

    // groups by color, background side and band.
    auto group = [&](bool sideways) {
        std::map<std::array<int, 4>, std::vector<std::size_t>> groups;
        for (std::size_t slot = 0; slot < boxes.size(); ++slot) {
            if (!boxes[slot].alive) continue;
            auto const& object = objects[boxes[slot].index];
            groups[{
                static_cast<int>(object.color),
                object.layer < 0,
                sideways ? boxes[slot].minY : boxes[slot].minX,
                sideways ? boxes[slot].maxY : boxes[slot].maxX
            }].push_back(slot);
        }
        return groups;
    };

    bool merged = true;
    while (merged) {
        merged = false;
        for (bool sideways : {true, false}) {
            for (auto& [key, slots] : group(sideways)) {
                std::sort(slots.begin(), slots.end(), [&](std::size_t left, std::size_t right) {
                    return sideways ? boxes[left].minX < boxes[right].minX
                                    : boxes[left].minY < boxes[right].minY;
                });
                for (std::size_t i = 0; i + 1 < slots.size(); ++i) {
                    auto& first = boxes[slots[i]];
                    auto& second = boxes[slots[i + 1]];
                    if (!first.alive) continue;
                    bool const touching = sideways
                        ? first.maxX == second.minX
                        : first.maxY == second.minY;
                    if (!touching) continue;
                    if (sideways) {
                        second.minX = first.minX;
                    } else {
                        second.minY = first.minY;
                    }
                    objects[second.index].layer = std::min(
                        objects[second.index].layer, objects[first.index].layer);
                    first.alive = false;
                    merged = true;
                }
            }
        }
    }

    std::vector<std::uint8_t> keep(objects.size(), 1);
    for (auto const& box : boxes) {
        if (box.alive) {
            auto& object = objects[box.index];
            object.x = (box.minX + box.maxX) * 0.5f;
            object.y = (box.minY + box.maxY) * 0.5f;
            object.width = static_cast<float>(box.maxX - box.minX);
            object.height = static_cast<float>(box.maxY - box.minY);
        } else {
            keep[box.index] = 0;
        }
    }
    compactKept(objects, keep);
    std::stable_sort(objects.begin(), objects.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
}

bool rectShape(Primitive const& object) {
    return std::abs(object.skew) < 0.001f &&
        (object.kind == PrimitiveKind::Block || object.kind == PrimitiveKind::Stroke);
}

bool rectPairShapes(Primitive const& first, Primitive const& second) {
    return rectShape(first) && rectShape(second);
}

// shared box of two rectangles plus its spare.
bool unitedRect(
    Primitive const& first,
    Primitive const& second,
    Primitive& result,
    float& extra,
    float& covered
) {
    if (std::abs(first.skew) > 0.001f || std::abs(second.skew) > 0.001f) return false;
    float difference = std::fmod(std::abs(first.rotation - second.rotation), 180.f);
    difference = std::min(difference, 180.f - difference);
    if (difference > 0.05f) return false;

    float const angle = first.rotation * kPi / 180.f;
    float const cosine = std::cos(angle);
    float const sine = std::sin(angle);
    struct ProjectedRect {
        float minMajor = 0.f;
        float maxMajor = 0.f;
        float minMinor = 0.f;
        float maxMinor = 0.f;
    };
    auto project = [&](Primitive const& object) {
        float const centerMajor = object.x * cosine + object.y * sine;
        float const centerMinor = -object.x * sine + object.y * cosine;
        float const delta = (object.rotation - first.rotation) * kPi / 180.f;
        float const majorExtent = std::abs(std::cos(delta)) * object.width * 0.5f +
            std::abs(std::sin(delta)) * object.height * 0.5f;
        float const minorExtent = std::abs(std::sin(delta)) * object.width * 0.5f +
            std::abs(std::cos(delta)) * object.height * 0.5f;
        return ProjectedRect{
            centerMajor - majorExtent, centerMajor + majorExtent,
            centerMinor - minorExtent, centerMinor + minorExtent
        };
    };

    auto const a = project(first);
    auto const b = project(second);
    float const minMajor = std::min(a.minMajor, b.minMajor);
    float const maxMajor = std::max(a.maxMajor, b.maxMajor);
    float const minMinor = std::min(a.minMinor, b.minMinor);
    float const maxMinor = std::max(a.maxMinor, b.maxMinor);
    float const overlapWidth = std::max(
        0.f, std::min(a.maxMajor, b.maxMajor) - std::max(a.minMajor, b.minMajor));
    float const overlapHeight = std::max(
        0.f, std::min(a.maxMinor, b.maxMinor) - std::max(a.minMinor, b.minMinor));
    float const unionArea = first.width * first.height + second.width * second.height -
        overlapWidth * overlapHeight;
    float const mergedArea = (maxMajor - minMajor) * (maxMinor - minMinor);
    extra = mergedArea - unionArea;
    covered = unionArea;

    float const centerMajor = (minMajor + maxMajor) * 0.5f;
    float const centerMinor = (minMinor + maxMinor) * 0.5f;
    result = {
        centerMajor * cosine - centerMinor * sine,
        centerMajor * sine + centerMinor * cosine,
        maxMajor - minMajor,
        maxMinor - minMinor,
        first.rotation,
        first.color,
        first.kind == PrimitiveKind::Block && second.kind == PrimitiveKind::Block
            ? PrimitiveKind::Block : PrimitiveKind::Stroke,
        std::max(first.layer, second.layer)
    };
    return true;
}

// same color never interleaves: joining on a low layer changes nothing.
bool mergeRectPair(
    Primitive const& first, Primitive const& second, Primitive& result,
    bool exactOnly = false
) {
    if (first.color != second.color || !rectPairShapes(first, second)) {
        return false;
    }
    if ((first.layer < 0) != (second.layer < 0)) return false;
    float extra = 0.f;
    float covered = 0.f;
    if (!unitedRect(first, second, result, extra, covered)) return false;
    // cross-layer joins exact only: no map backing the spare.
    float const slack = exactOnly ? 0.01f
        : first.layer == second.layer ? std::max(0.01f, covered * 0.002f)
                                      : 0.01f;
    if (extra > slack) return false;
    result.layer = std::min(first.layer, second.layer);
    return true;
}

void mergePaintRects(std::vector<Primitive>& objects, bool exactOnly = false) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t first = 0; first < objects.size() && !merged; ++first) {
            for (std::size_t second = first + 1; second < objects.size(); ++second) {
                Primitive replacement;
                if (!mergeRectPair(objects[first], objects[second], replacement, exactOnly)) {
                    continue;
                }
                objects[first] = replacement;
                objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(second));
                merged = true;
                break;
            }
        }
    }
}

// free spare when the drawing holds; foreign caps above.
void absorbPaintRects(
    std::vector<Primitive>& objects,
    int width,
    int height,
    std::vector<Primitive const*> const& foreign = {}
) {
    std::size_t const samples =
        static_cast<std::size_t>(width) * height * kPruneScale * kPruneScale;

    std::vector<std::uint8_t> occupied;
    if (!foreign.empty()) {
        occupied.assign(samples, 0);
        for (auto const* object : foreign) {
            anySample(*object, width, height, [&](std::size_t sample) {
                occupied[sample] = 1;
                return false;
            });
        }
    }

    bool absorbed = true;
    for (int round = 0; absorbed && round < kAbsorbRounds && objects.size() > 1; ++round) {
        absorbed = false;
        std::stable_sort(objects.begin(), objects.end(),
                         [](Primitive const& left, Primitive const& right) {
                             return left.layer < right.layer;
                         });

        std::vector<std::int32_t> top(samples, -1);
        auto const forms = xformsOf(objects);
        for (std::size_t index = 0; index < objects.size(); ++index) {
            anySample(forms[index], width, height, [&](std::size_t sample) {
                top[sample] = static_cast<std::int32_t>(index);
                return false;
            });
        }

        std::map<std::uint16_t, std::vector<std::size_t>> byColor;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            if (rectShape(objects[index])) byColor[objects[index].color].push_back(index);
        }

        std::vector<std::uint8_t> spent(objects.size(), 0);
        std::vector<Primitive> merges;
        std::vector<std::array<int, 4>> claimed;
        for (auto const& [color, group] : byColor) {
            for (std::size_t slot = 0; slot < group.size(); ++slot) {
                std::size_t const first = group[slot];
                if (spent[first]) continue;
                for (std::size_t other = slot + 1; other < group.size(); ++other) {
                    std::size_t const second = group[other];
                    if (spent[second]) continue;

                    Primitive candidate;
                    float extra = 0.f;
                    float covered = 0.f;
                    if (!unitedRect(
                            objects[first], objects[second], candidate, extra, covered)) {
                        continue;
                    }
                    // excessive void is no merge: steals next round's room.
                    if (extra > covered * kAbsorbSlack) continue;

                    // spare counts when it keeps the color or an upper layer covers it.
                    bool safe = true;
                    auto const candidateForm = xformOf(candidate);
                    anySample(candidateForm, width, height, [&](std::size_t sample) {
                        auto const owner = top[sample];
                        if (owner < 0) {
                            safe = false;
                            return true;
                        }
                        auto const slot = static_cast<std::size_t>(owner);
                        if (slot == first || slot == second) return false;
                        if (!occupied.empty() && occupied[sample]) {
                            safe = false;
                            return true;
                        }
                        if (objects[slot].color == candidate.color) return false;
                        if (objects[slot].layer > candidate.layer) return false;
                        safe = false;
                        return true;
                    });
                    if (!safe) continue;

                    // same-round merges must never touch each other.
                    auto const box = shapeBox(candidate, width, height);
                    bool overlaps = false;
                    for (auto const& taken : claimed) {
                        if (box[0] <= taken[2] && taken[0] <= box[2] &&
                            box[1] <= taken[3] && taken[1] <= box[3]) {
                            overlaps = true;
                            break;
                        }
                    }
                    if (overlaps) continue;

                    merges.push_back(candidate);
                    claimed.push_back(box);
                    spent[first] = 1;
                    spent[second] = 1;
                    absorbed = true;
                    break;
                }
            }
        }
        if (!absorbed) break;

        std::vector<Primitive> kept = std::move(merges);
        kept.reserve(kept.size() + objects.size());
        for (std::size_t index = 0; index < objects.size(); ++index) {
            if (!spent[index]) kept.push_back(objects[index]);
        }
        objects = std::move(kept);
    }
}

void appendRepairs(
    std::vector<Primitive>& output,
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& blocked,
    std::vector<int> const& allowed = {},
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true
) {
    if (positions.empty()) return;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> permitted(cells, 0);
    std::vector<std::uint8_t> target(cells, 0);
    auto const& paint = allowed.empty() ? positions : allowed;
    for (int position : paint) {
        permitted[static_cast<std::size_t>(position)] = 1;
        target[static_cast<std::size_t>(position)] = 1;
    }
    // pixel never forgives void centers; smooth relaxes the edge.
    for (auto const* mask : {&blocked}) {
        if (mask->size() != cells) continue;
        for (std::size_t position = 0; position < cells; ++position) {
            permitted[position] |= (*mask)[position];
        }
    }
    if (!gridExact && empty.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            permitted[position] |= empty[position];
        }
    }

    // spare: cells the rectangle may cross without changing the drawing.
    std::vector<std::uint8_t> spare(cells, 0);
    for (int position : paint) spare[static_cast<std::size_t>(position)] = 1;
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            spare[position] |= blocked[position];
        }
    }

    std::vector<int> leftover;
    for (auto const& component : connectedComponents(positions, width, height)) {
        // rectangular blob: goes to final packing.
        auto rectangle = packBlocks(component, width, height, color, spare);
        // one turned piece pays when straight would need several.
        if (rectangle.size() >= 2 && component.size() >= 4 &&
            appendSmallPatch(
                output, component, width, height, color, layer, permitted, blocked,
                gridExact)) {
            continue;
        }
        leftover.insert(leftover.end(), component.begin(), component.end());
    }
    if (leftover.empty()) return;
    auto plain = packBlocks(leftover, width, height, color, spare);
    for (auto& block : plain) block.layer = static_cast<std::int16_t>(layer);
    output.insert(output.end(), plain.begin(), plain.end());
}

// tiny turned piece: try disc, then straight block, then cell.
std::vector<int> normalizePaintSpikes(
    std::vector<Primitive>& objects,
    std::vector<int> const& target,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& /*blocked*/,
    std::vector<std::uint8_t> const& permitted,
    bool gridExact = true
) {
    // smooth allows side 4 to close curves into discs; pixel stays at 3.
    float const maxSpikeSide = gridExact ? 3.f : 4.f;
    constexpr float kStraightAngle = 7.f;
    std::vector<int> repairs;
    std::vector<Primitive> normalized;
    normalized.reserve(objects.size());
    // smooth fringe: turned strips only, computed once.
    std::vector<std::uint8_t> smoothNear;
    if (!gridExact) smoothNear = nearCells(target, width, height);
    for (auto const& object : objects) {
        float folded = std::fmod(std::abs(object.rotation), 90.f);
        folded = std::min(folded, 90.f - folded);
        bool const isRotatedStroke = object.kind == PrimitiveKind::Stroke;
        bool const isTriangle =
            object.kind == PrimitiveKind::Triangle ||
            object.kind == PrimitiveKind::WideTriangle;
        bool const isSpikeTriangle = isTriangle &&
            object.width <= maxSpikeSide && object.height <= maxSpikeSide;
        bool const isSpikeStroke = isRotatedStroke && folded > kStraightAngle &&
            object.width <= maxSpikeSide && object.height <= maxSpikeSide;

        if (!isSpikeTriangle && !isSpikeStroke) {
            normalized.push_back(object);
            continue;
        }
        // smooth keeps edges from 2 cells if they fit cleanly without spiking.
        if (!isSpikeTriangle && !gridExact &&
            fitsPaintOutline(object, permitted, smoothNear, width, height,
                             gridExact)) {
            normalized.push_back(object);
            continue;
        }

        bool replaced = false;
        // mixed sizes: the edge splinter never forces a square outside.
        for (float scale : {1.f, 0.9f, 0.75f, 0.6f, 0.5f}) {
            Primitive block{
                object.x, object.y, object.width * scale,
                object.height * scale, 0.f,
                static_cast<std::uint16_t>(color), PrimitiveKind::Block,
                object.layer};
            if (!fitsPaintBoundary(block, permitted, width, height, gridExact)) {
                continue;
            }
            normalized.push_back(block);
            replaced = true;
            break;
        }
        if (replaced) continue;

        // the center repacks straight on the next call.
        auto const targetForm = xformOf(object);
        for (int position : target) {
            float const px = static_cast<float>(position % width) + 0.5f;
            float const py = static_cast<float>(position / width) + 0.5f;
            if (targetForm.contains(px, py)) repairs.push_back(position);
        }
    }
    objects = std::move(normalized);
    std::sort(repairs.begin(), repairs.end());
    repairs.erase(std::unique(repairs.begin(), repairs.end()), repairs.end());
    return repairs;
}

// only the free tip gets a disc; hidden joints don't.
void roundExposedStrokeEnds(
    std::vector<Primitive>& objects,
    std::vector<int> const& target,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true
) {
    // strokes end cleanly with rectangular ends (as drawn in gd art):
    // never append tiny circle discs that show as incoherent dots.
    return;
    std::vector<std::uint8_t> targetMask(
        static_cast<std::size_t>(width) * height, 0);
    for (int position : target) {
        if (position >= 0 && position < width * height) {
            targetMask[static_cast<std::size_t>(position)] = 1;
        }
    }
    auto nearMask = nearCells(target, width, height);
    // paint-smooth relaxes void for the continuous edge; pixel never forgives.
    if (gridExact) maskVoid(nearMask, empty);
    auto targetGap = [&](Point point) {
        int const centerX = static_cast<int>(std::floor(point.x));
        int const centerY = static_cast<int>(std::floor(point.y));
        float best = 1e9f;
        for (int y = centerY - 3; y <= centerY + 3; ++y) {
            for (int x = centerX - 3; x <= centerX + 3; ++x) {
                if (x < 0 || y < 0 || x >= width || y >= height ||
                    !targetMask[static_cast<std::size_t>(y) * width + x]) {
                    continue;
                }
                float const dx = point.x < x ? static_cast<float>(x) - point.x
                    : point.x > x + 1.f ? point.x - static_cast<float>(x + 1) : 0.f;
                float const dy = point.y < y ? static_cast<float>(y) - point.y
                    : point.y > y + 1.f ? point.y - static_cast<float>(y + 1) : 0.f;
                best = std::min(best, std::hypot(dx, dy));
            }
        }
        return best;
    };
    auto continuesAlong = [&](Point center, Point direction, float thickness) {
        int const centerX = static_cast<int>(std::floor(center.x));
        int const centerY = static_cast<int>(std::floor(center.y));
        int const reach = std::max(2, static_cast<int>(std::ceil(thickness)) + 1);
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                int const x = centerX + dx;
                int const y = centerY + dy;
                if (x < 0 || y < 0 || x >= width || y >= height ||
                    !targetMask[static_cast<std::size_t>(y) * width + x]) {
                    continue;
                }
                Point const relative{
                    static_cast<float>(x) + 0.5f - center.x,
                    static_cast<float>(y) + 0.5f - center.y
                };
                float const along = relative.x * direction.x +
                    relative.y * direction.y;
                float const across = std::abs(
                    relative.x * direction.y - relative.y * direction.x);
                float const minimumAlong = std::max(thickness * 0.45f, 0.35f);
                float const maximumAcross = std::max(thickness * 0.85f, 0.65f);
                if (along > minimumAlong && across <= maximumAcross) return true;
            }
        }
        return false;
    };

    std::vector<Primitive> rounded;
    rounded.reserve(objects.size() + objects.size() / 3);
    for (auto const& object : objects) {
        if (object.kind != PrimitiveKind::Stroke || object.color != color || std::abs(object.skew) > 0.001f) {
            rounded.push_back(object);
            continue;
        }
        float folded = std::fmod(std::abs(object.rotation), 90.f);
        folded = std::min(folded, 90.f - folded);
        float const thickness = std::min(object.width, object.height);
        float const length = std::max(object.width, object.height);
        // short or square takes no disc; curved bands earn extra.
        if (folded <= 10.f || thickness < kVisibleRoundCapDiameter ||
            length <= thickness * 1.35f) {
            rounded.push_back(object);
            continue;
        }

        float const angle = object.rotation * kPi / 180.f;
        Point const direction{std::cos(angle), std::sin(angle)};
        bool capAt[2] = {false, false};
        Point capCenter[2];
        for (int end = 0; end < 2; ++end) {
            float const sign = end == 0 ? -1.f : 1.f;
            capCenter[end] = {
                object.x + direction.x * sign * (length * 0.5f - thickness * 0.5f),
                object.y + direction.y * sign * (length * 0.5f - thickness * 0.5f)
            };
            // color cell ahead: continuation, stays covered.
            if (continuesAlong(capCenter[end],
                              {direction.x * (end == 0 ? -1.f : 1.f),
                               direction.y * (end == 0 ? -1.f : 1.f)},
                              thickness)) {
                continue;
            }
            Point const normal{-direction.y, direction.x};
            Point const edge{
                object.x + direction.x * sign * length * 0.5f,
                object.y + direction.y * sign * length * 0.5f
            };
            // lines must not be dotted with circles: place a round joint disc
            // only when this endpoint meets another stroke at a corner or
            // curve turn (deflection > 31°, dot < 0.85).
            bool meetsAnotherStrokeAtCorner = false;
            for (auto const& other : objects) {
                if (&other == &object || other.kind != PrimitiveKind::Stroke || other.color != color) continue;
                float const dist = pointDistance({other.x, other.y}, capCenter[end]);
                float const otherLen = std::max(other.width, other.height);
                float const otherThick = std::min(other.width, other.height);
                if (dist <= otherLen * 0.5f + otherThick * 0.5f) {
                    float const otherAngle = other.rotation * kPi / 180.f;
                    Point const otherDir{std::cos(otherAngle), std::sin(otherAngle)};
                    float const dot = std::abs(direction.x * otherDir.x + direction.y * otherDir.y);
                    if (dot < 0.85f) {
                        meetsAnotherStrokeAtCorner = true;
                        break;
                    }
                }
            }
            if (!meetsAnotherStrokeAtCorner) continue;

            float const cornerGap = std::max(
                targetGap({edge.x + normal.x * thickness * 0.5f,
                           edge.y + normal.y * thickness * 0.5f}),
                targetGap({edge.x - normal.x * thickness * 0.5f,
                           edge.y - normal.y * thickness * 0.5f}));
            // only the detached peak gets capped; glued edges take the rectangle.
            float const gapThreshold = thickness <= 1.6f
                ? kExposedCapGap : 0.9f;
            if (cornerGap < gapThreshold) continue;

            bool const alreadyRound = std::any_of(
                objects.begin(), objects.end(), [&](Primitive const& other) {
                    return &other != &object && other.kind == PrimitiveKind::Circle &&
                        other.color == color &&
                        pointDistance({other.x, other.y}, capCenter[end]) <
                            std::min(other.width, thickness) * 0.55f;
                });
            if (alreadyRound) continue;
            Primitive const cap{
                capCenter[end].x, capCenter[end].y,
                thickness, thickness, 0.f,
                static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
                object.layer
            };
            if (coversBlocked(cap, width, height, blocked) ||
                !fitsPaintNear(cap, permitted, nearMask, width, height, gridExact)) {
                continue;
            }
            capAt[end] = true;
        }
        if (!capAt[0] && !capAt[1]) {
            rounded.push_back(object);
            continue;
        }

        Primitive center = object;
        if (capAt[0] && capAt[1]) {
            center.width = std::max(length - thickness, 0.05f);
            center.height = thickness;
        } else {
            center.width = std::max(length - thickness * 0.5f, 0.05f);
            center.height = thickness;
            float const shift = (capAt[0] ? 1.f : -1.f) * thickness * 0.25f;
            center.x += direction.x * shift;
            center.y += direction.y * shift;
        }
        std::vector<Primitive> replacement{center};
        for (int end = 0; end < 2; ++end) {
            if (!capAt[end]) continue;
            replacement.push_back({
                capCenter[end].x, capCenter[end].y,
                thickness, thickness, 0.f,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Circle,
                object.layer
            });
        }
        // smoothing never drops a center the strip covered.
        bool preservesCenters = true;
        auto const original = xformOf(object);
        auto const substitutes = xformsOf(replacement);
        for (int position : target) {
            float const px = static_cast<float>(position % width) + 0.5f;
            float const py = static_cast<float>(position / width) + 0.5f;
            if (!original.contains(px, py)) continue;
            if (std::none_of(substitutes.begin(), substitutes.end(), [&](ShapeXform const& shape) {
                    return shape.contains(px, py);
                })) {
                preservesCenters = false;
                break;
            }
        }
        if (!preservesCenters) {
            rounded.push_back(object);
            continue;
        }
        rounded.insert(rounded.end(), replacement.begin(), replacement.end());
    }
    objects = std::move(rounded);
}

bool paintFragment(Primitive const& object, float maximumLength) {
    bool const solid = object.kind == PrimitiveKind::Block ||
        object.kind == PrimitiveKind::Stroke || object.kind == PrimitiveKind::Circle;
    return solid && std::abs(object.skew) < 0.001f && object.layer >= 0 && object.width > 0.f && object.height > 0.f &&
        std::max(object.width, object.height) <= maximumLength &&
        std::min(object.width, object.height) <= 3.f;
}

struct PaintFragmentSamples {
    std::vector<Point> centers;
    std::vector<Point> required;
    bool interior = false;
};

PaintFragmentSamples samplePaintFragment(
    ShapeXform const& form,
    std::vector<std::uint8_t> const& target,
    int width,
    int height
) {
    PaintFragmentSamples samples;
    auto const box = xformBox(form, width, height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            auto const cell = static_cast<std::size_t>(y) * width + x;
            if (!target[cell]) continue;
            if (form.contains(x + 0.5f, y + 0.5f)) {
                samples.centers.push_back({x + 0.5f, y + 0.5f});
                samples.required.push_back(samples.centers.back());
                // shared edge and corner contacts keep thin curves connected between groups.
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int const nextX = x + dx, nextY = y + dy;
                        if ((dx == 0 && dy == 0) || nextX < 0 || nextY < 0 ||
                            nextX >= width || nextY >= height ||
                            !target[static_cast<std::size_t>(nextY) * width + nextX]) {
                            continue;
                        }
                        if (dx != 0 && dy != 0 &&
                            (target[static_cast<std::size_t>(y) * width + nextX] ||
                             target[static_cast<std::size_t>(nextY) * width + x])) {
                            continue;
                        }
                        Point const contact{x + 0.5f + dx * 0.5f, y + 0.5f + dy * 0.5f};
                        if (form.contains(contact.x, contact.y)) samples.required.push_back(contact);
                    }
                }
            }
            bool const interior = x > 0 && y > 0 && x + 1 < width && y + 1 < height &&
                target[cell - 1] && target[cell + 1] &&
                target[cell - width] && target[cell + width];
            samples.interior |= interior;
            // convex replacements cover each sampled row when they contain both endpoints.
            int const scale = interior ? kPruneScale : 4;
            int const first = interior ? 0 : 1;
            int const last = interior ? scale : 3;
            for (int sampleY = first; sampleY < last; ++sampleY) {
                float const py = y + (sampleY + 0.5f) / scale;
                int left = -1, right = -1;
                for (int sampleX = first; sampleX < last; ++sampleX) {
                    if (!form.contains(x + (sampleX + 0.5f) / scale, py)) continue;
                    if (left < 0) left = sampleX;
                    right = sampleX;
                }
                if (left < 0) continue;
                samples.required.push_back({x + (left + 0.5f) / scale, py});
                if (right != left) {
                    samples.required.push_back({x + (right + 0.5f) / scale, py});
                }
            }
        }
    }
    return samples;
}

std::optional<Primitive> fitPaintFragments(
    std::vector<int> const& group,
    std::vector<Primitive> const& objects,
    std::vector<ShapeXform> const& forms,
    std::vector<PaintFragmentSamples> const& samples,
    std::vector<std::uint8_t> const& target,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height
) {
    std::array<int, 4> box{width, height, -1, -1};
    float originalArea = 0.f;
    auto layer = objects[static_cast<std::size_t>(group.front())].layer;
    std::vector<Point> centers;
    std::vector<Point> required;
    bool hasInterior = false;
    for (int index : group) {
        auto const& object = objects[static_cast<std::size_t>(index)];
        auto const bounds = xformBox(forms[static_cast<std::size_t>(index)], width, height);
        box[0] = std::min(box[0], bounds[0]);
        box[1] = std::min(box[1], bounds[1]);
        box[2] = std::max(box[2], bounds[2]);
        box[3] = std::max(box[3], bounds[3]);
        originalArea += object.width * object.height *
            (object.kind == PrimitiveKind::Circle ? kPi * 0.25f : 1.f);
        layer = std::max(layer, object.layer);
        auto const& support = samples[static_cast<std::size_t>(index)];
        centers.insert(centers.end(), support.centers.begin(), support.centers.end());
        required.insert(required.end(), support.required.begin(), support.required.end());
        hasInterior |= support.interior;
    }
    if (box[2] - box[0] > 32 || box[3] - box[1] > 32) return std::nullopt;
    if (centers.size() < 2 || required.empty()) return std::nullopt;

    Point mean;
    for (auto const& point : centers) {
        mean.x += point.x;
        mean.y += point.y;
    }
    mean.x /= static_cast<float>(centers.size());
    mean.y /= static_cast<float>(centers.size());
    float xx = 0.f, xy = 0.f, yy = 0.f;
    float minX = centers.front().x, maxX = minX;
    float minY = centers.front().y, maxY = minY;
    for (auto const& point : centers) {
        float const dx = point.x - mean.x;
        float const dy = point.y - mean.y;
        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    // cell extents recover slopes that short stair runs bias in the covariance estimate.
    std::vector<float> angles{0.5f * std::atan2(2.f * xy, xx - yy), 0.f,
        std::atan2(std::copysign(maxY - minY + 1.f, xy), maxX - minX + 1.f)};
    for (int index : group) {
        auto const& object = objects[static_cast<std::size_t>(index)];
        if (object.kind == PrimitiveKind::Circle) continue;
        float const angle = object.rotation * kPi / 180.f;
        if (std::none_of(angles.begin(), angles.end(), [&](float known) {
                return std::abs(std::sin(angle - known)) < 0.02f;
            })) {
            angles.push_back(angle);
        }
        if (angles.size() >= 5) break;
    }

    std::optional<Primitive> best;
    float bestArea = std::numeric_limits<float>::max();
    auto consider = [&](Primitive candidate) {
        float const ratio = std::max(candidate.width, candidate.height) /
            std::max(std::min(candidate.width, candidate.height), 0.001f);
        // only round/compact patches (ratio <= 1.35) get the circle area
        // discount; elongated ones stay rectangular strokes.
        float const areaMultiplier = (candidate.kind == PrimitiveKind::Circle && ratio <= 1.35f)
            ? kPi * 0.25f : 1.f;
        float const area = candidate.width * candidate.height * areaMultiplier;
        if (area >= bestArea || area > originalArea * 1.18f + 0.25f ||
            std::max(candidate.width, candidate.height) > 32.f) {
            return;
        }
        auto const form = xformOf(candidate);
        if (std::any_of(required.begin(), required.end(), [&](Point const& point) {
                return !form.contains(point.x, point.y);
            })) {
            return;
        }
        // circles can render above rectangles from a different sprite sheet.
        auto const& boundary = candidate.kind == PrimitiveKind::Circle ? target : permitted;
        float const shortSide = std::min(candidate.width, candidate.height);
        float const longSide = std::max(candidate.width, candidate.height);
        // a one-cell diagonal has a larger fringe fraction even with no foreign center touched.
        float const spillLimit = candidate.kind == PrimitiveKind::Stroke &&
            shortSide <= 1.5f && longSide >= shortSide * 2.f ? 0.35f : kSmoothSpill;
        if (!fitsPaintBoundary(candidate, boundary, width, height, false, spillLimit)) return;
        best = candidate;
        bestArea = area;
    };

    auto const color = objects[static_cast<std::size_t>(group.front())].color;
    for (float angle : angles) {
        float const cosine = std::cos(angle);
        float const sine = std::sin(angle);
        float minMajor = std::numeric_limits<float>::max();
        float maxMajor = std::numeric_limits<float>::lowest();
        float minMinor = minMajor;
        float maxMinor = maxMajor;
        for (auto const& point : required) {
            float const dx = point.x - mean.x;
            float const dy = point.y - mean.y;
            float const major = dx * cosine + dy * sine;
            float const minor = -dx * sine + dy * cosine;
            minMajor = std::min(minMajor, major);
            maxMajor = std::max(maxMajor, major);
            minMinor = std::min(minMinor, minor);
            maxMinor = std::max(maxMinor, minor);
        }
        float const major = (minMajor + maxMajor) * 0.5f;
        float const minor = (minMinor + maxMinor) * 0.5f;
        Point const center{mean.x + major * cosine - minor * sine,
                           mean.y + major * sine + minor * cosine};
        for (float padding : {0.02f, 0.125f, 0.25f, 0.4f}) {
            if (hasInterior && padding < 0.125f) continue;
            float halfWidth = std::max(0.45f, (maxMajor - minMajor) * 0.5f + padding);
            float halfHeight = std::max(0.45f, (maxMinor - minMinor) * 0.5f + padding);
            if (halfWidth >= halfHeight * 2.f) {
                halfWidth = std::max(halfWidth, (maxMajor - minMajor) * 0.5f + 0.5f);
            } else if (halfHeight >= halfWidth * 2.f) {
                halfHeight = std::max(halfHeight, (maxMinor - minMinor) * 0.5f + 0.5f);
            }
            Primitive candidate{center.x, center.y, halfWidth * 2.f, halfHeight * 2.f,
                angle * 180.f / kPi, color, PrimitiveKind::Stroke, layer};
            consider(candidate);
        }
        float const halfWidth = std::max(0.45f, (maxMajor - minMajor) * 0.5f);
        float const halfHeight = std::max(0.45f, (maxMinor - minMinor) * 0.5f);
        if (std::max(halfWidth, halfHeight) <= std::min(halfWidth, halfHeight) * 1.35f) {
            float radius = 1.f;
            for (auto const& point : required) {
                float const dx = point.x - center.x, dy = point.y - center.y;
                radius = std::max(radius, std::hypot((dx * cosine + dy * sine) / halfWidth,
                    (-dx * sine + dy * cosine) / halfHeight));
            }
            consider({center.x, center.y, halfWidth * radius * 2.f + 0.01f,
                halfHeight * radius * 2.f + 0.01f, angle * 180.f / kPi,
                color, PrimitiveKind::Circle, layer});
        }
    }
    return best;
}

void coalescePaintFragments(
    std::vector<Primitive>& objects,
    std::vector<int> const& positions,
    int width,
    int height,
    std::vector<std::uint8_t> const& blocked
) {
    if (objects.size() < 2 || positions.size() < 2 || width <= 0 || height <= 0) return;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> target(cells, 0);
    for (int position : positions) {
        if (position >= 0 && static_cast<std::size_t>(position) < cells) {
            target[static_cast<std::size_t>(position)] = 1;
        }
    }
    auto permitted = target;
    if (blocked.size() == cells) {
        for (std::size_t cell = 0; cell < cells; ++cell) permitted[cell] |= blocked[cell];
    }

    constexpr int bucketSize = 4;
    int const columns = (width + bucketSize - 1) / bucketSize;
    int const rows = (height + bucketSize - 1) / bucketSize;
    for (int pass = 0; pass < 3; ++pass) {
        auto const forms = xformsOf(objects);
        std::vector<std::vector<int>> buckets(static_cast<std::size_t>(columns) * rows);
        std::vector<PaintFragmentSamples> samples(objects.size());
        std::vector<int> seeds;
        for (std::size_t index = 0; index < objects.size(); ++index) {
            auto const& object = objects[index];
            if (!paintFragment(object, 32.f)) continue;
            auto const box = xformBox(forms[index], width, height);
            if (box[2] < box[0] || box[3] < box[1]) continue;
            samples[index] = samplePaintFragment(forms[index], target, width, height);
            for (int y = box[1] / bucketSize; y <= box[3] / bucketSize; ++y) {
                for (int x = box[0] / bucketSize; x <= box[2] / bucketSize; ++x) {
                    buckets[static_cast<std::size_t>(y) * columns + x].push_back(
                        static_cast<int>(index));
                }
            }
            if (paintFragment(object, 6.f)) seeds.push_back(static_cast<int>(index));
        }
        std::stable_sort(seeds.begin(), seeds.end(), [&](int left, int right) {
            auto const& a = objects[static_cast<std::size_t>(left)];
            auto const& b = objects[static_cast<std::size_t>(right)];
            return a.width * a.height < b.width * b.height;
        });
        std::vector<std::uint8_t> keep(objects.size(), 1);
        std::vector<Primitive> additions;
        std::vector<int> seen(objects.size(), -1);
        for (int seed : seeds) {
            if (!keep[static_cast<std::size_t>(seed)]) continue;
            auto const box = xformBox(forms[static_cast<std::size_t>(seed)], width, height);
            std::vector<int> neighbors;
            for (int y = std::max(0, box[1] - 2) / bucketSize;
                 y <= std::min(height - 1, box[3] + 2) / bucketSize; ++y) {
                for (int x = std::max(0, box[0] - 2) / bucketSize;
                     x <= std::min(width - 1, box[2] + 2) / bucketSize; ++x) {
                    for (int index : buckets[static_cast<std::size_t>(y) * columns + x]) {
                        auto const slot = static_cast<std::size_t>(index);
                        if (index == seed || !keep[slot] || seen[slot] == seed ||
                            objects[slot].color != objects[static_cast<std::size_t>(seed)].color) {
                            continue;
                        }
                        seen[slot] = seed;
                        neighbors.push_back(index);
                    }
                }
            }
            auto const& origin = objects[static_cast<std::size_t>(seed)];
            std::stable_sort(neighbors.begin(), neighbors.end(), [&](int left, int right) {
                auto const& a = objects[static_cast<std::size_t>(left)];
                auto const& b = objects[static_cast<std::size_t>(right)];
                return std::hypot(a.x - origin.x, a.y - origin.y) <
                    std::hypot(b.x - origin.x, b.y - origin.y);
            });
            if (neighbors.size() > 24) neighbors.resize(24);
            std::vector<int> group{seed};
            std::optional<Primitive> fitted;
            for (std::size_t neighbor = 0; neighbor < neighbors.size(); ++neighbor) {
                int const index = neighbors[neighbor];
                if (std::find(group.begin(), group.end(), index) != group.end()) continue;
                auto const& next = forms[static_cast<std::size_t>(index)];
                bool const touches = std::any_of(group.begin(), group.end(), [&](int member) {
                    auto const& form = forms[static_cast<std::size_t>(member)];
                    return std::abs(form.x - next.x) <= form.extentX + next.extentX + 0.25f &&
                        std::abs(form.y - next.y) <= form.extentY + next.extentY + 0.25f;
                });
                if (!touches) continue;
                group.push_back(index);
                auto candidate = fitPaintFragments(
                    group, objects, forms, samples, target, permitted, width, height);
                // some stair slopes need a complete step before a straight fit is possible.
                if (!candidate && group.size() < 12) {
                    auto const last = std::min(neighbors.size(), neighbor + 5);
                    for (std::size_t lookahead = neighbor + 1; lookahead < last; ++lookahead) {
                        int const other = neighbors[lookahead];
                        if (std::find(group.begin(), group.end(), other) != group.end()) continue;
                        auto const& extra = forms[static_cast<std::size_t>(other)];
                        if (std::abs(next.x - extra.x) > next.extentX + extra.extentX + 0.25f ||
                            std::abs(next.y - extra.y) > next.extentY + extra.extentY + 0.25f) {
                            continue;
                        }
                        group.push_back(other);
                        candidate = fitPaintFragments(
                            group, objects, forms, samples, target, permitted, width, height);
                        if (candidate) break;
                        group.pop_back();
                    }
                }
                if (!candidate) {
                    group.pop_back();
                    continue;
                }
                fitted = candidate;
                if (group.size() >= 12) break;
            }
            if (!fitted) continue;
            for (int index : group) keep[static_cast<std::size_t>(index)] = 0;
            additions.push_back(*fitted);
        }
        if (additions.empty()) break;
        compactKept(objects, keep);
        objects.insert(objects.end(), additions.begin(), additions.end());
    }
}

void roundPaintFragmentEnds(
    std::vector<Primitive>& objects,
    std::vector<int> const& positions,
    int width,
    int height
) {
    if (objects.empty() || positions.empty() ||
        std::none_of(objects.begin(), objects.end(), [](Primitive const& object) {
            return object.kind == PrimitiveKind::Stroke && object.layer >= 0;
        })) {
        return;
    }
    std::vector<std::uint8_t> target(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) target[static_cast<std::size_t>(position)] = 1;
    auto const forms = xformsOf(objects);
    constexpr int bucketSize = 4;
    int const columns = (width + bucketSize - 1) / bucketSize;
    int const rows = (height + bucketSize - 1) / bucketSize;
    std::vector<std::vector<std::size_t>> buckets(static_cast<std::size_t>(columns) * rows);
    for (std::size_t index = 0; index < forms.size(); ++index) {
        auto const box = xformBox(forms[index], width, height);
        if (box[2] < box[0] || box[3] < box[1]) continue;
        for (int y = box[1] / bucketSize; y <= box[3] / bucketSize; ++y) {
            for (int x = box[0] / bucketSize; x <= box[2] / bucketSize; ++x) {
                buckets[static_cast<std::size_t>(y) * columns + x].push_back(index);
            }
        }
    }
    auto coveredByNeighbor = [&](Point point, std::size_t owner) {
        int const x = static_cast<int>(std::floor(point.x));
        int const y = static_cast<int>(std::floor(point.y));
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        auto const& bucket = buckets[static_cast<std::size_t>(y / bucketSize) * columns +
            x / bucketSize];
        return std::any_of(bucket.begin(), bucket.end(), [&](std::size_t index) {
            return index != owner && objects[index].color == objects[owner].color &&
                forms[index].contains(point.x, point.y);
        });
    };
    std::vector<Primitive> rounded;
    rounded.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto const& object = objects[index];
        float const length = std::max(object.width, object.height);
        float const thickness = std::min(object.width, object.height);
        if (object.kind != PrimitiveKind::Stroke || object.layer < 0 || std::abs(object.skew) > 0.001f ||
            thickness < kRoundCapMinThickness || thickness > 8.f ||
            length > 32.f || length <= thickness * 1.35f) {
            rounded.push_back(object);
            continue;
        }
        Primitive body = object;
        if (body.height > body.width) {
            std::swap(body.width, body.height);
            body.rotation += 90.f;
        }
        float const angle = body.rotation * kPi / 180.f;
        Point const direction{std::cos(angle), std::sin(angle)};
        Point const normal{-direction.y, direction.x};
        auto const support = samplePaintFragment(forms[index], target, width, height);
        if (support.centers.empty()) {
            rounded.push_back(object);
            continue;
        }
        std::vector<Primitive> caps;
        for (float sign : {-1.f, 1.f}) {
            Point const edge{object.x + direction.x * sign * length * 0.5f,
                object.y + direction.y * sign * length * 0.5f};
            Point const first{edge.x + normal.x * thickness * 0.49f,
                edge.y + normal.y * thickness * 0.49f};
            Point const second{edge.x - normal.x * thickness * 0.49f,
                edge.y - normal.y * thickness * 0.49f};
            if (coveredByNeighbor(first, index) && coveredByNeighbor(second, index)) continue;
            for (float ratio : {0.5f, 0.35f, 0.25f}) {
                float const depth = thickness * ratio;
                Primitive cap{edge.x - direction.x * sign * depth,
                    edge.y - direction.y * sign * depth,
                    depth * 2.f, thickness, 0.f, object.color,
                    PrimitiveKind::Circle, object.layer};
                if (std::min(cap.width, cap.height) < kVisibleRoundCapDiameter) {
                    continue;
                }
                if (!fitsPaintBoundary(cap, target, width, height, false)) continue;
                Primitive candidate = body;
                if (candidate.width - depth <= 1.6f && candidate.height <= 1.6f) continue;
                candidate.width -= depth;
                candidate.x -= direction.x * sign * depth * 0.5f;
                candidate.y -= direction.y * sign * depth * 0.5f;
                auto const candidateForm = xformOf(candidate);
                auto const capForm = xformOf(cap);
                auto const capForms = xformsOf(caps);
                // preserve interior samples and thin-line contacts while removing exposed corners.
                bool const preserves = std::all_of(support.required.begin(), support.required.end(),
                    [&](Point point) {
                        return candidateForm.contains(point.x, point.y) ||
                            capForm.contains(point.x, point.y) ||
                            std::any_of(capForms.begin(), capForms.end(), [&](ShapeXform const& form) {
                                return form.contains(point.x, point.y);
                            });
                    });
                if (!preserves) continue;
                body = candidate;
                caps.push_back(cap);
                break;
            }
        }
        rounded.push_back(caps.empty() ? object : body);
        rounded.insert(rounded.end(), caps.begin(), caps.end());
    }
    objects = std::move(rounded);
}

void finishPaintFragments(
    std::vector<Primitive>& objects,
    std::vector<int> const& positions,
    int width,
    int height,
    std::vector<std::uint8_t> const& blocked
) {
    coalescePaintFragments(objects, positions, width, height, blocked);
    roundPaintFragmentEnds(objects, positions, width, height);
}

// only objects with no visible contribution can be discarded.
void dropRedundantObjects(std::vector<Primitive>& objects, int width, int height) {
    std::size_t const samples =
        static_cast<std::size_t>(width) * height * kPruneScale * kPruneScale;

    bool dropped = true;
    while (dropped && objects.size() > 1) {
        dropped = false;
        std::vector<Primitive const*> ordered;
        ordered.reserve(objects.size());
        for (auto const& object : objects) ordered.push_back(&object);
        std::stable_sort(ordered.begin(), ordered.end(), [](auto* left, auto* right) {
            return left->layer < right->layer;
        });

        std::vector<std::int32_t> top(samples, -1);
        std::vector<std::int32_t> below(samples, -1);
        std::vector<ShapeXform> passForms;
        passForms.reserve(ordered.size());
        for (auto const* entry : ordered) passForms.push_back(xformOf(*entry));
        for (std::size_t slot = 0; slot < ordered.size(); ++slot) {
            anySample(passForms[slot], width, height, [&](std::size_t sample) {
                below[sample] = top[sample];
                top[sample] = static_cast<std::int32_t>(slot);
                return false;
            });
        }

        std::vector<int> visibleCount(ordered.size(), 0);
        for (std::size_t sample = 0; sample < samples; ++sample) {
            auto const owner = top[sample];
            if (owner < 0) continue;
            auto const under = below[sample];
            if (under >= 0 &&
                ordered[static_cast<std::size_t>(under)]->color ==
                    ordered[static_cast<std::size_t>(owner)]->color) {
                continue;
            }
            ++visibleCount[static_cast<std::size_t>(owner)];
        }

        std::vector<std::uint8_t> keep(objects.size(), 0);
        for (std::size_t slot = 0; slot < ordered.size(); ++slot) {
            if (visibleCount[slot] <= 0 &&
                std::min(ordered[slot]->width, ordered[slot]->height) >= 1.f / kPruneScale) {
                dropped = true;
                continue;
            }
            keep[static_cast<std::size_t>(ordered[slot] - objects.data())] = 1;
        }
        if (dropped) compactKept(objects, keep);
    }
}

} // namespace

void pruneMicroObjects(std::vector<Primitive>& objects, int /*width*/, int /*height*/) {
    // thin positive-area pieces are real drawing geometry: a fixed minimum
    // thickness erased outlines and opened seams between joined fills.
    std::erase_if(objects, [](Primitive const& object) {
        return !std::isfinite(object.x) || !std::isfinite(object.y) ||
            !std::isfinite(object.width) || !std::isfinite(object.height) ||
            !std::isfinite(object.rotation) || !std::isfinite(object.skew) ||
            object.width <= 0.f || object.height <= 0.f;
    });
}

std::vector<Primitive> paintSeamRepairs(
    std::vector<Primitive> const& objects,
    std::vector<std::int32_t> const& cells,
    std::vector<int> const& ranks,
    int width,
    int height
) {
    std::vector<Primitive> repairs;
    if (width < 3 || height < 3 ||
        cells.size() != static_cast<std::size_t>(width) * height || ranks.empty()) {
        return repairs;
    }
    std::vector<int> targets;
    targets.reserve(cells.size());
    for (int position = 0; position < width * height; ++position) {
        int const color = cells[static_cast<std::size_t>(position)];
        if (color >= 0 && color < static_cast<int>(ranks.size())) {
            targets.push_back(position);
        }
    }
    if (targets.empty()) return repairs;

    std::vector<int> interior;
    for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
            int const position = y * width + x;
            int const color = cells[static_cast<std::size_t>(position)];
            if (color < 0 || color >= static_cast<int>(ranks.size()) ||
                cells[static_cast<std::size_t>(position - 1)] < 0 ||
                cells[static_cast<std::size_t>(position + 1)] < 0 ||
                cells[static_cast<std::size_t>(position - width)] < 0 ||
                cells[static_cast<std::size_t>(position + width)] < 0) {
                continue;
            }
            interior.push_back(position);
        }
    }
    std::vector<std::uint8_t> isInterior(cells.size(), 0);
    for (int position : interior) {
        isInterior[static_cast<std::size_t>(position)] = 1;
    }
    // single color: curved edges belong to the figure; seams run between colors only.
    std::vector<int> repairTargets;
    repairTargets.reserve(targets.size());
    for (int position : targets) {
        if (isInterior[static_cast<std::size_t>(position)] || ranks.size() > 1) {
            repairTargets.push_back(position);
        }
    }
    auto problemCells = [&](
        std::vector<Primitive> const& visible,
        int scale,
        std::vector<int> const& candidates
    ) {
        int const scaledWidth = width * scale;
        int const scaledHeight = height * scale;
        std::vector<std::int16_t> ownerColor(
            static_cast<std::size_t>(scaledWidth) * scaledHeight, -1);
        std::vector<std::int16_t> ownerLayer(
            ownerColor.size(), std::numeric_limits<std::int16_t>::min());
        std::vector<Primitive const*> ordered;
        ordered.reserve(visible.size());
        for (auto const& object : visible) ordered.push_back(&object);
        std::stable_sort(ordered.begin(), ordered.end(), [](auto* left, auto* right) {
            return left->layer < right->layer;
        });
        for (auto const* object : ordered) {
            forEachSample(xformOf(*object), width, height, scale, [&](int x, int y) {
                std::size_t const sample = static_cast<std::size_t>(y) * scaledWidth + x;
                ownerColor[sample] = static_cast<std::int16_t>(object->color);
                ownerLayer[sample] = object->layer;
                return false;
            });
        }

        std::vector<std::vector<int>> problems(ranks.size());
        for (int position : candidates) {
            int const x = position % width;
            int const y = position / width;
            int const expected = cells[static_cast<std::size_t>(position)];
            bool problem = false;
            int const firstSampleX = isInterior[static_cast<std::size_t>(position)]
                ? 0 : scale / 2;
            int const lastSampleX = isInterior[static_cast<std::size_t>(position)]
                ? scale : firstSampleX + 1;
            int const firstSampleY = isInterior[static_cast<std::size_t>(position)]
                ? 0 : scale / 2;
            int const lastSampleY = isInterior[static_cast<std::size_t>(position)]
                ? scale : firstSampleY + 1;
            for (int sampleY = firstSampleY; sampleY < lastSampleY && !problem; ++sampleY) {
                for (int sampleX = firstSampleX; sampleX < lastSampleX; ++sampleX) {
                    std::size_t const sample = static_cast<std::size_t>(
                        y * scale + sampleY) * scaledWidth + x * scale + sampleX;
                    if (ownerColor[sample] < 0 ||
                        (ownerLayer[sample] < 0 && ownerColor[sample] != expected)) {
                        problem = true;
                        break;
                    }
                }
            }
            if (problem) problems[static_cast<std::size_t>(expected)].push_back(position);
        }
        return problems;
    };

    std::vector<Primitive> working = objects;
    auto problems = problemCells(working, 8, repairTargets);
    bool const hasProblem = std::any_of(
        problems.begin(), problems.end(), [](auto const& positions) {
            return !positions.empty();
        });
    if (!hasProblem) return repairs;

    bool const hasUnderpaint = std::any_of(
        working.begin(), working.end(), [](Primitive const& object) {
            return object.layer < 0;
        });
    if (!hasUnderpaint) {
        auto underpaint = packBlocks(interior, width, height, 0);
        for (auto& object : underpaint) {
            int const minX = static_cast<int>(std::lround(object.x - object.width * 0.5f));
            int const minY = static_cast<int>(std::lround(object.y - object.height * 0.5f));
            int const maxX = minX + static_cast<int>(std::lround(object.width));
            int const maxY = minY + static_cast<int>(std::lround(object.height));
            std::vector<int> usage(ranks.size(), 0);
            for (int y = minY; y < maxY; ++y) {
                for (int x = minX; x < maxX; ++x) {
                    int const color = cells[static_cast<std::size_t>(y) * width + x];
                    if (color >= 0 && color < static_cast<int>(usage.size())) {
                        ++usage[static_cast<std::size_t>(color)];
                    }
                }
            }
            object.color = static_cast<std::uint16_t>(std::distance(
                usage.begin(), std::max_element(usage.begin(), usage.end())));
            // per-color underpaint below the drawing: breaks ties between colors.
            object.layer = static_cast<std::int16_t>(
                -1 - (static_cast<int>(ranks.size()) - 1 -
                      ranks[static_cast<std::size_t>(object.color)]));
        }
        repairs.insert(repairs.end(), underpaint.begin(), underpaint.end());
        working.insert(working.end(), underpaint.begin(), underpaint.end());
        problems = problemCells(working, 12, repairTargets);
    }

    for (int color = 0; color < static_cast<int>(problems.size()); ++color) {
        std::vector<int> blockPositions;
        blockPositions.reserve(problems[static_cast<std::size_t>(color)].size());
        for (int position : problems[static_cast<std::size_t>(color)]) {
            if (isInterior[static_cast<std::size_t>(position)]) {
                blockPositions.push_back(position);
                continue;
            }
            // 1x1 block patch for seam repair
            Primitive patch{
                position % width + 0.5f,
                position / width + 0.5f,
                1.f,
                1.f,
                0.f,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Block,
                static_cast<std::int16_t>(
                    ranks[static_cast<std::size_t>(color)] * kPaintSublayers + 2)
            };
            repairs.push_back(patch);
        }
        auto patches = packBlocks(blockPositions, width, height, color);
        for (auto& patch : patches) {
            patch.layer = static_cast<std::int16_t>(
                ranks[static_cast<std::size_t>(color)] * kPaintSublayers + 2);
            repairs.push_back(patch);
        }
    }
    std::stable_sort(repairs.begin(), repairs.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
    return repairs;
}

// sews same-color same-turn caps into rectangles without overpainting.
void mergePaintSolids(std::vector<Primitive>& objects) {
    if (objects.size() < 2) return;
    mergePaintBlocks(objects);
    mergePaintRects(objects, true);
}

void smoothPaintFragments(
    std::vector<Primitive>& objects,
    std::vector<std::int32_t> const& cells,
    std::vector<int> const& ranks,
    int width,
    int height
) {
    if (width <= 0 || height <= 0 ||
        cells.size() != static_cast<std::size_t>(width) * height || ranks.empty()) {
        return;
    }
    std::vector<std::vector<int>> positions(ranks.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        int const color = cells[cell];
        if (color >= 0 && color < static_cast<int>(ranks.size())) {
            positions[static_cast<std::size_t>(color)].push_back(static_cast<int>(cell));
        }
    }
    std::vector<std::vector<Primitive>> byColor(ranks.size());
    std::vector<Primitive> result;
    result.reserve(objects.size());
    for (auto const& object : objects) {
        if (object.layer < 0 || object.color >= ranks.size()) {
            result.push_back(object);
        } else {
            byColor[object.color].push_back(object);
        }
    }
    std::vector<std::uint8_t> blocked(cells.size(), 0);
    for (std::size_t color = 0; color < ranks.size(); ++color) {
        auto& fragments = byColor[color];
        if (fragments.size() >= 2 && positions[color].size() >= 2) {
            for (std::size_t cell = 0; cell < cells.size(); ++cell) {
                int const other = cells[cell];
                blocked[cell] = other >= 0 && other < static_cast<int>(ranks.size()) &&
                    ranks[static_cast<std::size_t>(other)] > ranks[color];
            }
            finishPaintFragments(fragments, positions[color], width, height, blocked);
        }
        result.insert(result.end(), fragments.begin(), fragments.end());
    }
    std::stable_sort(result.begin(), result.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
    objects = std::move(result);
}

void prunePaintObjects(std::vector<Primitive>& objects, int width, int height) {
    if (objects.size() < 2) return;
    std::vector<std::uint8_t> keep(objects.size(), 0);
    std::vector<PruneEntry> entries;
    entries.reserve(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        entries.push_back({&objects[index], &keep[index]});
    }
    markUsefulObjects(std::move(entries), width, height);
    compactKept(objects, keep);
    mergePaintBlocks(objects);
    mergePaintRects(objects);
    absorbPaintRects(objects, width, height);
    dropRedundantObjects(objects, width, height);
    pruneMicroObjects(objects, width, height);
    dropRedundantObjects(objects, width, height);
}

void prunePaintObjectsByVisibility(
    std::vector<Primitive>& staticObjects,
    std::vector<VisibilityTrack>& tracks,
    int frameCount,
    int width,
    int height
) {
    frameCount = std::max(frameCount, 1);
    std::vector<std::uint8_t> keepStatic(staticObjects.size(), 0);
    std::vector<std::vector<std::uint8_t>> keepTracks;
    keepTracks.reserve(tracks.size());
    for (auto const& track : tracks) {
        keepTracks.emplace_back(track.objects.size(), 0);
    }

    // objects stay when some frame needs them.
    for (int frame = 0; frame < frameCount; ++frame) {
        std::vector<PruneEntry> entries;
        entries.reserve(staticObjects.size());
        for (std::size_t index = 0; index < staticObjects.size(); ++index) {
            entries.push_back({&staticObjects[index], &keepStatic[index]});
        }
        for (std::size_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
            auto const& track = tracks[trackIndex];
            std::size_t const word = static_cast<std::size_t>(frame / 64);
            if (word >= track.mask.size() ||
                (track.mask[word] & (std::uint64_t{1} << (frame % 64))) == 0) {
                continue;
            }
            for (std::size_t index = 0; index < track.objects.size(); ++index) {
                entries.push_back({
                    &track.objects[index],
                    &keepTracks[trackIndex][index]
                });
            }
        }
        markUsefulObjects(std::move(entries), width, height);
    }

    compactKept(staticObjects, keepStatic);
    mergePaintBlocks(staticObjects);
    mergePaintRects(staticObjects);
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        compactKept(tracks[track].objects, keepTracks[track]);
        // each track merges apart: other frames aren't the same.
        mergePaintBlocks(tracks[track].objects);
        mergePaintRects(tracks[track].objects);
        absorbPaintRects(tracks[track].objects, width, height);
        dropRedundantObjects(tracks[track].objects, width, height);
        pruneMicroObjects(tracks[track].objects, width, height);
        dropRedundantObjects(tracks[track].objects, width, height);
    }

    // tracks as forbidden ground: grown fixed objects never step on them.
    std::vector<Primitive const*> animated;
    for (auto const& track : tracks) {
        for (auto const& object : track.objects) animated.push_back(&object);
    }
    absorbPaintRects(staticObjects, width, height, animated);
    dropRedundantObjects(staticObjects, width, height);
    pruneMicroObjects(staticObjects, width, height);
    dropRedundantObjects(staticObjects, width, height);
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](auto const& track) {
        return track.objects.empty();
    }), tracks.end());
}

std::vector<int> paintOrder(
    std::vector<GridFrame> const& frames,
    int colors,
    int width,
    int height
) {
    std::vector<int> ranks(static_cast<std::size_t>(std::max(colors, 1)), 0);
    if (colors <= 1 || frames.empty()) return ranks;

    struct Entry {
        int color = 0;
        float depth = 0.f;
        std::size_t area = 0;
        bool background = false;
    };
    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(colors));

    std::vector<std::vector<int>> masks(static_cast<std::size_t>(colors));
    std::vector<int> lastSeen(static_cast<std::size_t>(colors), -1);
    for (int position = 0; position < width * height; ++position) {
        for (auto const& frame : frames) {
            int const color = frame.cells[static_cast<std::size_t>(position)];
            if (color < 0 || color >= colors) continue;
            if (lastSeen[static_cast<std::size_t>(color)] == position) continue;
            lastSeen[static_cast<std::size_t>(color)] = position;
            masks[static_cast<std::size_t>(color)].push_back(position);
        }
    }

    // enclosed goes on top; cells changing color don't constrain.
    std::vector<std::int16_t> owner(static_cast<std::size_t>(width) * height, -1);
    for (int color = 0; color < colors; ++color) {
        for (int position : masks[static_cast<std::size_t>(color)]) {
            auto& cell = owner[static_cast<std::size_t>(position)];
            if (cell == -1) {
                cell = static_cast<std::int16_t>(color);
            } else if (cell >= 0 && cell != color) {
                cell = -2;
            }
        }
    }
    std::vector<std::vector<std::uint8_t>> mustPrecede(
        static_cast<std::size_t>(colors),
        std::vector<std::uint8_t>(static_cast<std::size_t>(colors), 0));
    auto addEnclosureRelations = [&](int color) {
        for (auto const& component : connectedComponents(
                 masks[static_cast<std::size_t>(color)], width, height)) {
            if (component.empty()) continue;
            bool touchesOutside = false;
            std::vector<std::uint8_t> neighbors(static_cast<std::size_t>(colors), 0);
            for (int position : component) {
                int const x = position % width;
                int const y = position / width;
                if (x == 0 || y == 0 || x + 1 == width || y + 1 == height) {
                    touchesOutside = true;
                    break;
                }
                for (auto const [dx, dy] : std::array<std::pair<int, int>, 4>{
                         std::pair{-1, 0}, std::pair{1, 0},
                         std::pair{0, -1}, std::pair{0, 1}}) {
                    int const xx = x + dx;
                    int const yy = y + dy;
                    auto const adjacent = owner[static_cast<std::size_t>(yy) * width + xx];
                    if (adjacent == -2) {
                        touchesOutside = true;
                    } else if (adjacent >= 0 && adjacent != color) {
                        neighbors[static_cast<std::size_t>(adjacent)] = 1;
                    } else if (adjacent < 0) {
                        // -1 is transparency/outside; -2 is scratch blend.
                        touchesOutside = true;
                    }
                }
            }
            if (touchesOutside) continue;
            for (int outer = 0; outer < colors; ++outer) {
                if (neighbors[static_cast<std::size_t>(outer)] != 0) {
                    mustPrecede[static_cast<std::size_t>(outer)]
                                [static_cast<std::size_t>(color)] = 1;
                }
            }
        }
    };
    for (int color = 0; color < colors; ++color) addEnclosureRelations(color);

    for (int color = 0; color < colors; ++color) {
        auto const& positions = masks[static_cast<std::size_t>(color)];
        if (positions.empty()) {
            entries.push_back({color, 0.f, 0});
            continue;
        }
        auto const region = buildRegion(positions, width);
        double total = 0.0;
        int border = 0;
        for (int position : positions) {
            int const x = position % width - region.offsetX;
            int const y = position / width - region.offsetY;
            total += region.distance[static_cast<std::size_t>(y) * region.width + x];
            int const sourceX = position % width;
            int const sourceY = position / width;
            border += sourceX == 0 || sourceY == 0 ||
                sourceX == width - 1 || sourceY == height - 1;
        }
        entries.push_back({
            color,
            static_cast<float>(total / static_cast<double>(positions.size())),
            positions.size(),
            border > width + height - 2
        });
    }

    // topology over heuristics; priority breaks the cycle.
    std::sort(entries.begin(), entries.end(), [](Entry const& left, Entry const& right) {
        if (left.background != right.background) return left.background;
        if (std::abs(left.depth - right.depth) > 0.001f) return left.depth > right.depth;
        if (left.area != right.area) return left.area > right.area;
        return left.color < right.color;
    });
    auto const baseOrder = [&](int color) {
        return std::find_if(entries.begin(), entries.end(), [&](Entry const& entry) {
            return entry.color == color;
        }) - entries.begin();
    };
    std::vector<int> indegree(static_cast<std::size_t>(colors), 0);
    for (int below = 0; below < colors; ++below) {
        for (int above = 0; above < colors; ++above) {
            if (mustPrecede[static_cast<std::size_t>(below)]
                           [static_cast<std::size_t>(above)] != 0) {
                ++indegree[static_cast<std::size_t>(above)];
            }
        }
    }
    std::vector<Entry> ordered;
    ordered.reserve(entries.size());
    std::vector<std::uint8_t> emitted(static_cast<std::size_t>(colors), 0);
    while (ordered.size() < entries.size()) {
        int chosen = -1;
        for (auto const& entry : entries) {
            int const color = entry.color;
            if (emitted[static_cast<std::size_t>(color)] != 0 ||
                indegree[static_cast<std::size_t>(color)] != 0) {
                continue;
            }
            if (chosen < 0 || baseOrder(color) < baseOrder(chosen)) chosen = color;
        }
        if (chosen < 0) {
            for (auto const& entry : entries) {
                int const color = entry.color;
                if (emitted[static_cast<std::size_t>(color)] == 0 &&
                    (chosen < 0 || baseOrder(color) < baseOrder(chosen))) {
                    chosen = color;
                }
            }
        }
        emitted[static_cast<std::size_t>(chosen)] = 1;
        ordered.push_back(entries[static_cast<std::size_t>(baseOrder(chosen))]);
        for (int above = 0; above < colors; ++above) {
            if (mustPrecede[static_cast<std::size_t>(chosen)]
                           [static_cast<std::size_t>(above)] != 0) {
                --indegree[static_cast<std::size_t>(above)];
            }
        }
    }
    entries = std::move(ordered);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ranks[static_cast<std::size_t>(entries[i].color)] = static_cast<int>(i);
    }
    return ranks;
}

std::vector<Primitive> vectorizeRoundedPaintStrokes(
    std::vector<int> const& positions, int width, int height, int color, int layer,
    std::vector<std::uint8_t> const& coveredLater, std::span<float const> sourceCoverage) {
    if (positions.empty() || width <= 0 || height <= 0) return {};
    std::vector<std::uint8_t> permitted(static_cast<std::size_t>(width) * height, 0);
    for (int cell : positions) permitted[cell] = 1;
    if (coveredLater.size() == permitted.size())
        for (std::size_t cell = 0; cell < permitted.size(); ++cell) permitted[cell] |= coveredLater[cell];
    StrokeCoverageField const coverage{width, height, sourceCoverage};
    std::vector<Primitive> output;
    bool traced = false;
    for (auto const& component : connectedComponents(positions, width, height)) {
        auto const region = buildRegion(component, width);
        if (appendCircle(output, region, color, layer, width, height, coveredLater)) continue;
        float const radius = *std::max_element(region.distance.begin(), region.distance.end());
        std::vector<Primitive> strokes;
        if (radius > 12.f || !appendChain(strokes, region, component, width, height, radius,
            color, layer, coveredLater, permitted, {}, false, 0.25f)) {
            auto blocks = packBlocks(component, width, height, color, permitted);
            for (auto& block : blocks) block.layer = static_cast<std::int16_t>(layer);
            output.insert(output.end(), blocks.begin(), blocks.end());
            continue;
        }
        traced = true;
        auto const fringeMask = nearCells(component, width, height);
        StrokeWidthSampler const sampler{region, coverage, radius * 3.f + 1.f};
        std::vector<Primitive> caps;
        for (auto body : strokes) {
            // appendChain overlaps square ends; round joins meet at the
            // actual axis endpoint, with no projecting rectangle corner.
            float const overlap = std::min(body.height * 0.25f, 0.35f);
            body.width = std::max(0.05f, body.width - overlap * 2.f);
            float const angle = body.rotation * kPi / 180.f;
            float const cosine = std::cos(angle), sine = std::sin(angle);
            body.height = sampler.thickness(body);
            for (int attempt = 0; attempt < 12 && !fitsStrokeFringe(body, permitted, fringeMask, width, height); ++attempt)
                body.height *= 0.9f;
            if (!fitsStrokeFringe(body, permitted, fringeMask, width, height)) continue;
            for (float end : {-0.5f, 0.5f}) {
                Primitive cap{body.x + end * body.width * cosine,
                    body.y + end * body.width * sine, body.height, body.height,
                    0.f, static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
                    static_cast<std::int16_t>(layer)};
                for (int attempt = 0; attempt < 12 && !fitsStrokeFringe(cap, permitted, fringeMask, width, height); ++attempt) {
                    cap.width *= 0.9f;
                    cap.height = cap.width;
                }
                if (!fitsStrokeFringe(cap, permitted, fringeMask, width, height)) continue;
                auto existing = std::find_if(caps.begin(), caps.end(), [&](auto const& other) {
                    return std::hypot(other.x - cap.x, other.y - cap.y) < 0.2f;
                });
                if (existing == caps.end()) caps.push_back(cap);
                else if (cap.width > existing->width) *existing = cap;
            }
            output.push_back(body);
        }
        output.insert(output.end(), caps.begin(), caps.end());
    }
    return traced ? output : std::vector<Primitive>{};
}

std::vector<Primitive> vectorizePaint(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& empty,
    bool gridExact
) {
    std::vector<Primitive> output;
    // each color's fills and outlines must stay below the next color; a
    // global outline layer let an earlier color cut across later highlights.
    int const fillLayer = rank * kPaintSublayers;
    int const strokeLayer = fillLayer + 1;
    int const base = fillLayer;
    // spare: cells the rectangle crosses without changing the drawing.
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> spare(cells, 0);
    for (int position : positions) {
        if (position >= 0 && static_cast<std::size_t>(position) < cells) {
            spare[static_cast<std::size_t>(position)] = 1;
        }
    }
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            spare[position] |= blocked[position];
        }
    }
    if (gridExact) {
        auto blocks = packBlocks(positions, width, height, color, spare);
        for (auto& block : blocks) block.layer = static_cast<std::int16_t>(base);
        return blocks;
    }
    std::vector<Primitive> fittedBox;
    bool fitted = false;
    if (positions.size() >= 8) {
        for (auto angle : {std::optional<float>{}, std::optional<float>{0.f}, std::optional<float>{kPi / 4.f}}) {
            if (appendSmallPatch(fittedBox, positions, width, height, color, base, spare, blocked, false, angle)) {
                fitted = true;
                break;
            }
        }
    }
    if (fitted) {
        auto& box = fittedBox.front();
        float const quarter = std::fmod(std::abs(box.rotation), 90.f);
        if (std::min(quarter, 90.f - quarter) < 0.001f) {
            if (std::abs(std::sin(box.rotation * kPi / 180.f)) > 0.5f) std::swap(box.width, box.height);
            box.rotation = 0.f;
            box.kind = PrimitiveKind::Block;
        }
        return fittedBox;
    }
    if (appendColumnRibbons(output, positions, width, height, color, strokeLayer, spare)) return output;
    // pixel never forgives void; smooth relaxes it for the edge.
    std::vector<std::uint8_t> permitted = spare;
    if (!gridExact && empty.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            permitted[position] |= empty[position];
        }
    }
    if (positions.size() >= 8) {
        auto const region = buildRegion(positions, width);
        if (appendCircle(
                output, region, color, base,
                width, height, blocked) ||
            appendTriangle(output, positions, width, height, color, base, blocked) ||
            appendCapsule(
                output, positions, width, height, color, base + 1,
                blocked, empty)) {
            if (!gridExact) finishPaintFragments(output, positions, width, height, blocked);
            return output;
        }
    }
    std::vector<std::vector<int>> pieces;
    for (auto const& whole : connectedComponents(positions, width, height)) {
        // silhouette before thickness: two discs make no ellipse.
        if (whole.size() >= 8 && whole.size() != positions.size()) {
            auto const region = buildRegion(whole, width);
            std::vector<Primitive> fitted;
            if (appendCircle(fitted, region, color, base,
                             width, height, blocked) ||
                appendTriangle(fitted, whole, width, height, color, base, blocked) ||
                appendCapsule(fitted, whole, width, height, color, base + 1,
                              blocked, empty)) {
                if (!gridExact) finishPaintFragments(fitted, whole, width, height, blocked);
                output.insert(output.end(), fitted.begin(), fitted.end());
                continue;
            }
        }
        for (auto& piece : splitByThickness(whole, width, height, kThickSpan)) {
            pieces.push_back(std::move(piece));
        }
    }

    std::vector<int> repairs;
    for (auto const& component : pieces) {
        if (component.size() <= 3) {
            repairs.insert(repairs.end(), component.begin(), component.end());
            continue;
        }

        auto blocks = packBlocks(component, width, height, color, spare);
        if (blocks.size() == 1) {
            blocks.front().layer = static_cast<std::int16_t>(base);
            output.push_back(blocks.front());
            continue;
        }

        auto const region = buildRegion(component, width);
        float radius = 0.f;
        for (int position : component) {
            int const x = position % width - region.offsetX;
            int const y = position / width - region.offsetY;
            radius = std::max(
                radius, region.distance[static_cast<std::size_t>(y) * region.width + x]);
        }

        std::vector<Primitive> shapes;
        std::vector<std::uint8_t> inside;
        // same color: one pass joins big rectangles.
        std::vector<int> plain;
        auto collectPlain = [&](
            std::vector<std::uint8_t> covered,
            std::vector<std::uint8_t> const& limit,
            bool requireMask,
            float minimumDepth
        ) {
            for (int position : plain) {
                int const x = position % width - region.offsetX;
                int const y = position / width - region.offsetY;
                covered[static_cast<std::size_t>(y) * region.width + x] = 1;
            }
            auto taken = selectCells(
                region, width, covered, limit, requireMask, minimumDepth);
            plain.insert(plain.end(), taken.begin(), taken.end());
        };

        bool chained = false;
        bool const fitted = appendCircle(
            shapes, region, color, base,
            width, height, blocked) ||
            appendTriangle(shapes, component, width, height, color, base, blocked) ||
            appendCapsule(
                shapes, component, width, height, color, base + 1, blocked, empty);
        if (fitted) {
            output.insert(output.end(), shapes.begin(), shapes.end());
            continue;
        }
            // chains only for strokes; compact goes outline.
            chained = radius <= kThinRadius &&
                appendChain(
                    shapes, region, component, width, height, radius, color, strokeLayer,
                    blocked, permitted, empty, gridExact);
            if (!chained) {
                float const band = std::clamp(radius * 1.4f, 1.4f, kBandWidth);
                std::vector<Contour> refined;
                for (auto const& contour : traceContours(region)) {
                    if (contour.points.size() < 3) continue;
                    // thin strips: trim little or peaks show.
                    float const local =
                        contourThickness(region, contour, band, gridExact);
                    refined.push_back(refineContour(
                        contour, local <= 2.5f ? 1 : 2,
                        gridExact ? std::clamp(local * 0.3f, 0.3f, kSmoothTolerance)
                                  : std::clamp(local * 0.15f, 0.18f, 0.45f)));
                }
                std::vector<Primitive> outline;
                for (auto const& contour : refined) {
                    appendBand(
                        outline, region, contour, band, color, strokeLayer,
                        width, height, blocked, permitted, gridExact);
                }
                // fringe on turned strips only; on straight it only adds spill.
                auto nearMask = nearCells(component, width, height);
                // paint-smooth relaxes void for the continuous edge; pixel never forgives.
                if (gridExact) maskVoid(nearMask, empty);
                if (!gridExact) {
                    for (auto& stroke : outline) {
                        if (!fitsPaintOutline(stroke, permitted, nearMask, width, height, gridExact)) {
                            float const origHeight = stroke.height;
                            float const origWidth = stroke.width;
                            for (float scale : {0.9f, 0.82f, 0.75f}) {
                                stroke.height = origHeight * scale;
                                stroke.width = origWidth * scale;
                                if (fitsPaintOutline(stroke, permitted, nearMask, width, height, gridExact)) {
                                    break;
                                }
                            }
                        }
                    }
                }
                outline.erase(std::remove_if(outline.begin(), outline.end(),
                    [&](Primitive const& stroke) {
                        return !fitsPaintOutline(
                            stroke, permitted, nearMask, width, height, gridExact);
                    }), outline.end());
                inside = insideContours(region, refined);
                // spilled fill bites the neighbor: out only when covering another.
                for (int y = 0; y < region.height; ++y) {
                    for (int x = 0; x < region.width; ++x) {
                        std::size_t const index =
                            static_cast<std::size_t>(y) * region.width + x;
                        if (!inside[index] || region.cells[index]) continue;
                        int const cellX = x + region.offsetX;
                        int const cellY = y + region.offsetY;
                        if (cellX < 0 || cellY < 0 || cellX >= width || cellY >= height) {
                            continue;
                        }
                        auto const cell = static_cast<std::size_t>(cellY) * width + cellX;
                        bool const coverable =
                            (blocked.size() == static_cast<std::size_t>(width) * height &&
                             blocked[cell]) ||
                            (empty.size() == static_cast<std::size_t>(width) * height &&
                             empty[cell]);
                        if (!coverable) inside[index] = 0;
                    }
                }
                collectPlain(
                    coverageMask(region, outline, true, gridExact), inside, false,
                    0.f);
                shapes.insert(shapes.end(), outline.begin(), outline.end());
            }

        collectPlain(
            coverageMask(region, shapes, false), inside, true, chained ? 1.1f : 1.6f);
        // fill under half-covered cells; chains cover it already.
        if (!chained) {
            collectPlain(
                coverageMask(region, shapes, true, gridExact), {}, true, 1.01f);
        }
        appendBlocks(shapes, plain, width, height, color, base, spare);
        // voids here show the neighbor: no mask, so no holes remain.
        auto missing = selectCells(
            region, width, coverageMask(region, shapes, !gridExact ? true : false, gridExact), {}, true, 0.f);
        repairs.insert(repairs.end(), missing.begin(), missing.end());
        output.insert(output.end(), shapes.begin(), shapes.end());
    }

    appendRepairs(
        output, repairs, width, height, color, fillLayer + 1, blocked, positions, empty,
        gridExact);

    // last barrier: patches can leave splinters too.
    roundExposedStrokeEnds(
        output, positions, width, height, color, blocked, permitted, empty, gridExact);
    auto spikeRepairs = normalizePaintSpikes(
        output, positions, width, height, color, blocked, permitted, gridExact);
    if (!spikeRepairs.empty()) {
        appendBlocks(output, spikeRepairs, width, height, color, fillLayer + 1);
    }

    if (!gridExact) finishPaintFragments(output, positions, width, height, blocked);

    std::stable_sort(output.begin(), output.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
    return output;
}

} // namespace paimon::gifimport
