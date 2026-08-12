#include "GifPaintVectorizer.hpp"

#include "GifArtVectorizer.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace paimon::gifimport {

namespace {

constexpr float kBandWidth = 3.f;
constexpr float kOvershoot = 0.5f;
constexpr float kSmoothTolerance = 0.9f;
constexpr float kThinRadius = 1.75f;
constexpr int kPadding = 2;

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

void transformRow(
    std::vector<float>& source,
    std::vector<float>& target,
    std::vector<int>& hull,
    std::vector<float>& breaks,
    int count
) {
    constexpr float kInfinity = std::numeric_limits<float>::max();
    int top = 0;
    hull[0] = 0;
    breaks[0] = -kInfinity;
    breaks[1] = kInfinity;
    for (int q = 1; q < count; ++q) {
        float split = 0.f;
        while (true) {
            int const p = hull[top];
            split = ((source[static_cast<std::size_t>(q)] + static_cast<float>(q) * q) -
                     (source[static_cast<std::size_t>(p)] + static_cast<float>(p) * p)) /
                    (2.f * static_cast<float>(q - p));
            if (split > breaks[static_cast<std::size_t>(top)] || top == 0) break;
            --top;
        }
        ++top;
        hull[static_cast<std::size_t>(top)] = q;
        breaks[static_cast<std::size_t>(top)] = split;
        breaks[static_cast<std::size_t>(top) + 1] = kInfinity;
    }
    top = 0;
    for (int q = 0; q < count; ++q) {
        while (breaks[static_cast<std::size_t>(top) + 1] < static_cast<float>(q)) ++top;
        int const p = hull[static_cast<std::size_t>(top)];
        float const offset = static_cast<float>(q - p);
        target[static_cast<std::size_t>(q)] = offset * offset + source[static_cast<std::size_t>(p)];
    }
}

void computeDistance(Region& region) {
    constexpr float kInfinity = 1e18f;
    std::size_t const total = static_cast<std::size_t>(region.width) * region.height;
    region.distance.assign(total, 0.f);
    std::vector<float> work(total, 0.f);
    for (std::size_t i = 0; i < total; ++i) work[i] = region.cells[i] ? kInfinity : 0.f;

    int const span = std::max(region.width, region.height);
    std::vector<float> source(static_cast<std::size_t>(span));
    std::vector<float> target(static_cast<std::size_t>(span));
    std::vector<int> hull(static_cast<std::size_t>(span));
    std::vector<float> breaks(static_cast<std::size_t>(span) + 1);

    for (int x = 0; x < region.width; ++x) {
        for (int y = 0; y < region.height; ++y) {
            source[static_cast<std::size_t>(y)] = work[static_cast<std::size_t>(y) * region.width + x];
        }
        transformRow(source, target, hull, breaks, region.height);
        for (int y = 0; y < region.height; ++y) {
            work[static_cast<std::size_t>(y) * region.width + x] = target[static_cast<std::size_t>(y)];
        }
    }
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            source[static_cast<std::size_t>(x)] = work[static_cast<std::size_t>(y) * region.width + x];
        }
        transformRow(source, target, hull, breaks, region.width);
        for (int x = 0; x < region.width; ++x) {
            region.distance[static_cast<std::size_t>(y) * region.width + x] =
                std::sqrt(target[static_cast<std::size_t>(x)]);
        }
    }
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

bool insideShape(Primitive const& object, float x, float y) {
    if (object.width <= 0.f || object.height <= 0.f) return false;
    float const angle = object.rotation * kPi / 180.f;
    float const cosine = std::cos(angle);
    float const sine = std::sin(angle);
    float const dx = x - object.x;
    float const dy = y - object.y;
    float const localX = dx * cosine + dy * sine;
    float const localY = -dx * sine + dy * cosine;
    if (object.kind == PrimitiveKind::Circle) {
        float const nx = localX / (object.width * 0.5f);
        float const ny = localY / (object.height * 0.5f);
        return nx * nx + ny * ny <= 1.f;
    }
    return std::abs(localX) <= object.width * 0.5f &&
        std::abs(localY) <= object.height * 0.5f;
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

std::vector<Point> smoothLoop(std::vector<Point> const& loop, int iterations) {
    std::vector<Point> current = loop;
    for (int pass = 0; pass < iterations; ++pass) {
        std::vector<Point> next;
        next.reserve(current.size() * 2);
        for (std::size_t i = 0; i < current.size(); ++i) {
            auto const& first = current[i];
            auto const& second = current[(i + 1) % current.size()];
            next.push_back({
                first.x * 0.75f + second.x * 0.25f,
                first.y * 0.75f + second.y * 0.25f
            });
            next.push_back({
                first.x * 0.25f + second.x * 0.75f,
                first.y * 0.25f + second.y * 0.75f
            });
        }
        current = std::move(next);
    }
    return current;
}

std::vector<Point> smoothPath(std::vector<Point> const& path, int iterations) {
    std::vector<Point> current = path;
    for (int pass = 0; pass < iterations && current.size() > 2; ++pass) {
        std::vector<Point> next;
        next.reserve(current.size() * 2);
        next.push_back(current.front());
        for (std::size_t i = 0; i + 1 < current.size(); ++i) {
            auto const& first = current[i];
            auto const& second = current[i + 1];
            next.push_back({
                first.x * 0.75f + second.x * 0.25f,
                first.y * 0.75f + second.y * 0.25f
            });
            next.push_back({
                first.x * 0.25f + second.x * 0.75f,
                first.y * 0.25f + second.y * 0.75f
            });
        }
        next.push_back(current.back());
        current = std::move(next);
    }
    return current;
}

std::vector<Point> simplifyLoop(std::vector<Point> const& loop, float tolerance) {
    if (loop.size() < 4) return loop;
    Point centroid;
    for (auto const& point : loop) {
        centroid.x += point.x;
        centroid.y += point.y;
    }
    centroid.x /= static_cast<float>(loop.size());
    centroid.y /= static_cast<float>(loop.size());

    std::size_t anchor = 0;
    float farthest = -1.f;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        float const distance = pointDistance(loop[i], centroid);
        if (distance > farthest) {
            farthest = distance;
            anchor = i;
        }
    }

    std::vector<Point> chain;
    chain.reserve(loop.size() + 1);
    for (std::size_t i = 0; i <= loop.size(); ++i) {
        chain.push_back(loop[(anchor + i) % loop.size()]);
    }
    auto reduced = simplify(chain, tolerance);
    if (reduced.size() > 1) reduced.pop_back();
    return reduced;
}

Contour refineContour(Contour const& contour, int iterations, float tolerance) {
    if (!contour.closed) {
        return {simplify(smoothPath(contour.points, iterations), tolerance), false};
    }
    return {simplifyLoop(smoothLoop(contour.points, iterations), tolerance), true};
}

constexpr float kRoundJoinDot = 0.82f;

float directionDot(Point const& incoming, Point const& outgoing) {
    return std::clamp(incoming.x * outgoing.x + incoming.y * outgoing.y, -1.f, 1.f);
}

float miterExtension(float dot, float thickness) {
    if (dot <= kRoundJoinDot) return 0.f;
    return thickness * 0.5f * std::tan(std::acos(dot) * 0.5f);
}

void appendJoin(
    std::vector<Primitive>& output,
    Point const& position,
    float diameter,
    int color,
    int layer
) {
    output.push_back({
        position.x,
        position.y,
        diameter,
        diameter,
        0.f,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Circle,
        static_cast<std::int16_t>(layer)
    });
}

struct Segment {
    Point direction;
    float length = 0.f;
};

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

void appendBand(
    std::vector<Primitive>& output,
    Region const& region,
    Contour const& contour,
    float band,
    int color,
    int layer
) {
    auto const& loop = contour.points;
    if (loop.size() < 2) return;
    std::size_t const segments = contour.closed ? loop.size() : loop.size() - 1;
    auto const measured = measure(loop, segments);
    float const offset = band * 0.5f - kOvershoot;

    for (std::size_t i = 0; i < segments; ++i) {
        auto const& segment = measured[i];
        if (segment.length <= 0.05f) continue;
        bool const hasPrevious = contour.closed || i > 0;
        bool const hasNext = contour.closed || i + 1 < segments;
        float const startDot = hasPrevious
            ? directionDot(measured[(i + segments - 1) % segments].direction, segment.direction)
            : 1.f;
        float const endDot = hasNext
            ? directionDot(segment.direction, measured[(i + 1) % segments].direction)
            : 1.f;
        float const startExtent = hasPrevious ? miterExtension(startDot, band) : band * 0.5f;
        float const endExtent = hasNext ? miterExtension(endDot, band) : band * 0.5f;

        auto const& first = loop[i];
        auto const& second = loop[(i + 1) % loop.size()];
        float const shift = (endExtent - startExtent) * 0.5f;
        float const midX = (first.x + second.x) * 0.5f;
        float const midY = (first.y + second.y) * 0.5f;
        float inwardX = -segment.direction.y;
        float inwardY = segment.direction.x;
        if (!region.filledAt(midX + inwardX * 0.75f, midY + inwardY * 0.75f)) {
            inwardX = -inwardX;
            inwardY = -inwardY;
        }
        output.push_back({
            midX + segment.direction.x * shift + inwardX * offset +
                static_cast<float>(region.offsetX),
            midY + segment.direction.y * shift + inwardY * offset +
                static_cast<float>(region.offsetY),
            segment.length + startExtent + endExtent,
            band,
            std::atan2(segment.direction.y, segment.direction.x) * 180.f / kPi,
            static_cast<std::uint16_t>(color),
            PrimitiveKind::Stroke,
            static_cast<std::int16_t>(layer)
        });

        if (!hasNext || endDot > kRoundJoinDot) continue;
        appendJoin(
            output,
            {second.x + inwardX * offset + static_cast<float>(region.offsetX),
             second.y + inwardY * offset + static_cast<float>(region.offsetY)},
            band, color, layer);
    }
}

void appendChain(
    std::vector<Primitive>& output,
    Region const& region,
    std::vector<int> const& component,
    int sourceWidth,
    float radius,
    int color,
    int layer
) {
    auto const skeleton = thin(component, sourceWidth);
    auto const paths = skeletonPaths(skeleton);
    float const tolerance = std::clamp(radius * 0.7f, 0.55f, 1.3f);

    std::vector<std::vector<Point>> lines;
    double span = 0.0;
    for (auto const& path : paths) {
        std::vector<Point> points;
        points.reserve(path.size());
        for (int position : path) {
            points.push_back({
                static_cast<float>(position % skeleton.width + skeleton.offsetX) + 0.5f,
                static_cast<float>(position / skeleton.width + skeleton.offsetY) + 0.5f
            });
        }
        auto reduced = simplify(smoothPath(points, 1), tolerance);
        if (reduced.size() < 2) continue;
        for (std::size_t i = 1; i < reduced.size(); ++i) {
            span += pointDistance(reduced[i - 1], reduced[i]);
        }
        lines.push_back(std::move(reduced));
    }
    if (lines.empty()) return;

    float const nominal = std::clamp(
        static_cast<float>(static_cast<double>(component.size()) / std::max(span, 0.001)),
        0.8f, radius * 2.4f);

    for (auto const& reduced : lines) {
        std::size_t const segments = reduced.size() - 1;
        auto const measured = measure(reduced, segments);
        for (std::size_t i = 0; i < segments; ++i) {
            auto const& segment = measured[i];
            if (segment.length <= 0.05f) continue;
            auto const& first = reduced[i];
            auto const& second = reduced[i + 1];
            float const midX = (first.x + second.x) * 0.5f;
            float const midY = (first.y + second.y) * 0.5f;
            float const local = region.distanceAt(
                midX - static_cast<float>(region.offsetX),
                midY - static_cast<float>(region.offsetY));
            float const thickness = std::clamp(
                local * 2.f - 0.35f, nominal * 0.8f, std::max(nominal * 1.35f, 0.9f));
            float const endDot = i + 1 < segments
                ? directionDot(segment.direction, measured[i + 1].direction)
                : 1.f;
            float const startExtent = i > 0
                ? miterExtension(
                      directionDot(measured[i - 1].direction, segment.direction), thickness)
                : thickness * 0.5f;
            float const endExtent = i + 1 < segments
                ? miterExtension(endDot, thickness)
                : thickness * 0.5f;
            float const shift = (endExtent - startExtent) * 0.5f;
            output.push_back({
                midX + segment.direction.x * shift,
                midY + segment.direction.y * shift,
                segment.length + startExtent + endExtent,
                thickness,
                std::atan2(segment.direction.y, segment.direction.x) * 180.f / kPi,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Stroke,
                static_cast<std::int16_t>(layer)
            });

            if (endDot > kRoundJoinDot) continue;
            appendJoin(output, second, thickness, color, layer);
        }
    }
}

bool appendCircle(
    std::vector<Primitive>& output,
    Region const& region,
    std::size_t area,
    int color,
    int layer
) {
    float const boxWidth = static_cast<float>(region.width - kPadding * 2);
    float const boxHeight = static_cast<float>(region.height - kPadding * 2);
    if (boxWidth < 4.f || boxHeight < 4.f) return false;
    float const aspect = std::max(boxWidth, boxHeight) / std::min(boxWidth, boxHeight);
    if (aspect > 1.8f) return false;

    Primitive const circle{
        static_cast<float>(region.offsetX) + kPadding + boxWidth * 0.5f,
        static_cast<float>(region.offsetY) + kPadding + boxHeight * 0.5f,
        boxWidth,
        boxHeight,
        0.f,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Circle,
        static_cast<std::int16_t>(layer)
    };

    int missing = 0;
    int spilled = 0;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            float const sampleX = static_cast<float>(x + region.offsetX) + 0.5f;
            float const sampleY = static_cast<float>(y + region.offsetY) + 0.5f;
            bool const covered = insideShape(circle, sampleX, sampleY);
            if (region.filled(x, y)) {
                if (!covered) ++missing;
            } else if (covered) {
                ++spilled;
            }
        }
    }
    float const limit = static_cast<float>(area) * 0.08f;
    if (static_cast<float>(spilled) > limit || static_cast<float>(missing) > limit) return false;
    output.push_back(circle);
    return true;
}

std::vector<std::uint8_t> exteriorCells(
    Region const& region,
    std::vector<Primitive> const& band
) {
    std::size_t const total = static_cast<std::size_t>(region.width) * region.height;
    std::vector<std::uint8_t> blocked(total, 0);
    constexpr std::array<float, 3> kSamples{0.2f, 0.5f, 0.8f};
    for (auto const& object : band) {
        float const angle = object.rotation * kPi / 180.f;
        float const extentX = std::abs(std::cos(angle)) * object.width * 0.5f +
            std::abs(std::sin(angle)) * object.height * 0.5f;
        float const extentY = std::abs(std::sin(angle)) * object.width * 0.5f +
            std::abs(std::cos(angle)) * object.height * 0.5f;
        int const minX = std::max(0, static_cast<int>(
            std::floor(object.x - extentX)) - region.offsetX);
        int const minY = std::max(0, static_cast<int>(
            std::floor(object.y - extentY)) - region.offsetY);
        int const maxX = std::min(region.width - 1, static_cast<int>(
            std::ceil(object.x + extentX)) - region.offsetX);
        int const maxY = std::min(region.height - 1, static_cast<int>(
            std::ceil(object.y + extentY)) - region.offsetY);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
                if (blocked[index]) continue;
                for (float offsetY : kSamples) {
                    for (float offsetX : kSamples) {
                        if (!insideShape(
                                object,
                                static_cast<float>(x + region.offsetX) + offsetX,
                                static_cast<float>(y + region.offsetY) + offsetY)) {
                            continue;
                        }
                        blocked[index] = 1;
                        break;
                    }
                    if (blocked[index]) break;
                }
            }
        }
    }

    std::vector<std::uint8_t> exterior(total, 0);
    std::vector<int> pending;
    auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= region.width || y >= region.height) return;
        std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
        if (exterior[index] || blocked[index]) return;
        exterior[index] = 1;
        pending.push_back(static_cast<int>(index));
    };
    for (int x = 0; x < region.width; ++x) {
        push(x, 0);
        push(x, region.height - 1);
    }
    for (int y = 0; y < region.height; ++y) {
        push(0, y);
        push(region.width - 1, y);
    }
    while (!pending.empty()) {
        int const index = pending.back();
        pending.pop_back();
        int const x = index % region.width;
        int const y = index / region.width;
        push(x - 1, y);
        push(x + 1, y);
        push(x, y - 1);
        push(x, y + 1);
    }
    return exterior;
}

std::vector<int> interiorCells(
    Region const& region,
    int sourceWidth,
    float depth,
    std::vector<std::uint8_t> const& exterior
) {
    std::vector<int> positions;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
            if (region.distance[index] < depth || exterior[index]) continue;
            positions.push_back(
                (y + region.offsetY) * sourceWidth + x + region.offsetX);
        }
    }
    return positions;
}

std::vector<int> uncoveredCells(
    Region const& region,
    int sourceWidth,
    std::vector<Primitive> const& objects,
    float minimumDepth,
    std::vector<std::uint8_t> const& exterior
) {
    std::vector<std::uint8_t> covered(
        static_cast<std::size_t>(region.width) * region.height, 0);
    for (auto const& object : objects) {
        float const angle = object.rotation * kPi / 180.f;
        float const extentX = std::abs(std::cos(angle)) * object.width * 0.5f +
            std::abs(std::sin(angle)) * object.height * 0.5f;
        float const extentY = std::abs(std::sin(angle)) * object.width * 0.5f +
            std::abs(std::cos(angle)) * object.height * 0.5f;
        int const minX = std::max(0, static_cast<int>(
            std::floor(object.x - extentX)) - region.offsetX);
        int const minY = std::max(0, static_cast<int>(
            std::floor(object.y - extentY)) - region.offsetY);
        int const maxX = std::min(region.width - 1, static_cast<int>(
            std::ceil(object.x + extentX)) - region.offsetX);
        int const maxY = std::min(region.height - 1, static_cast<int>(
            std::ceil(object.y + extentY)) - region.offsetY);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
                if (covered[index] || !region.cells[index]) continue;
                float const sampleX = static_cast<float>(x + region.offsetX) + 0.5f;
                float const sampleY = static_cast<float>(y + region.offsetY) + 0.5f;
                if (insideShape(object, sampleX, sampleY)) covered[index] = 1;
            }
        }
    }

    std::vector<int> positions;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * region.width + x;
            if (!region.cells[index] || covered[index]) continue;
            if (region.distance[index] < minimumDepth) continue;
            if (!exterior.empty() && exterior[index]) continue;
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
    int layer
) {
    if (positions.empty()) return;
    auto blocks = packBlocks(positions, width, height, color);
    for (auto& block : blocks) {
        block.layer = static_cast<std::int16_t>(layer);
        output.push_back(block);
    }
}

} // namespace

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
    };
    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(colors));

    for (int color = 0; color < colors; ++color) {
        std::vector<int> positions;
        for (int position = 0; position < width * height; ++position) {
            for (auto const& frame : frames) {
                if (frame.cells[static_cast<std::size_t>(position)] != color) continue;
                positions.push_back(position);
                break;
            }
        }
        if (positions.empty()) {
            entries.push_back({color, 0.f, 0});
            continue;
        }
        auto const region = buildRegion(positions, width);
        double total = 0.0;
        for (int position : positions) {
            int const x = position % width - region.offsetX;
            int const y = position / width - region.offsetY;
            total += region.distance[static_cast<std::size_t>(y) * region.width + x];
        }
        entries.push_back({
            color,
            static_cast<float>(total / static_cast<double>(positions.size())),
            positions.size()
        });
    }

    std::sort(entries.begin(), entries.end(), [](Entry const& left, Entry const& right) {
        if (std::abs(left.depth - right.depth) > 0.001f) return left.depth > right.depth;
        if (left.area != right.area) return left.area > right.area;
        return left.color < right.color;
    });
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ranks[static_cast<std::size_t>(entries[i].color)] = static_cast<int>(i);
    }
    return ranks;
}

std::vector<Primitive> vectorizePaint(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank
) {
    std::vector<Primitive> output;
    int const base = rank * kPaintSublayers;
    for (auto const& component : connectedComponents(positions, width, height)) {
        if (component.size() <= 3) {
            appendBlocks(output, component, width, height, color, base);
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
        std::vector<std::uint8_t> exterior;
        bool const chained = radius <= kThinRadius;
        if (chained) {
            appendChain(shapes, region, component, width, radius, color, base + 1);
        } else if (!appendCircle(shapes, region, component.size(), color, base)) {
            float const band = std::clamp(radius * 1.4f, 1.4f, kBandWidth);
            float const depth = std::max(band - kOvershoot - 0.6f, 0.9f);
            std::vector<Primitive> outline;
            for (auto const& contour : traceContours(region)) {
                if (contour.points.size() < 3) continue;
                appendBand(
                    outline, region, refineContour(contour, 2, kSmoothTolerance),
                    band, color, base + 1);
            }
            exterior = exteriorCells(region, outline);
            appendBlocks(
                shapes, interiorCells(region, width, depth, exterior),
                width, height, color, base);
            shapes.insert(shapes.end(), outline.begin(), outline.end());
        }

        appendBlocks(
            shapes,
            uncoveredCells(region, width, shapes, chained ? 1.15f : 1.3f, exterior),
            width, height, color, base + 2);
        output.insert(output.end(), shapes.begin(), shapes.end());
    }

    std::stable_sort(output.begin(), output.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
    return output;
}

} // namespace paimon::gifimport
