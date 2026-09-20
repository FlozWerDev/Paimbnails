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
// Inglete de banda por lado: 0.0 devuelve picos en juntas sin extension.
constexpr float kBandMiter = 0.15f;
constexpr float kOvershoot = 0.72f;
constexpr float kFreeOvershoot = 0.2f;
constexpr float kSmoothTolerance = 0.9f;
constexpr float kThinRadius = 4.25f;
// Ancho a partir del cual la mancha ya no es trazo sino zona maciza.
constexpr int kThickSpan = 5;
// Fragmentos cortos van por cadena o reparos, no por losa.
constexpr int kLongSpan = 16;
// Diagonal llena su caja; ele/ese dejan media caja vacia que tapa lo de abajo.
constexpr float kPatchSlack = 1.7f;
// Asomar sobre capa tapada cuesta menos que sobre el dibujo.
constexpr float kCoveredSpill = 0.35f;
// Largo minimo en grosores para que compense trazar como trazo.
constexpr float kChainSlenderness = 3.f;
// Tope de derrame de tira antes de tirarla.
constexpr float kChainSpill = 0.06f;
// Suave pisa media celda vecina: derrame 15% y corte recta/girada a 14 grados.
constexpr float kSmoothSpill = 0.15f;
constexpr float kSmoothStraightAngle = 14.f;
// Borrado final de cadena: 0.007 es el minimo de picos sin mover geometria.
constexpr float kChainEraseSpill = 0.007f;
// Disco solo si el pico del bisel asoma 0.5 celdas sobre lo que el disco tapa.
constexpr float kRoundJointExcess = 0.5f;
// En trazos de 1 celda el disco entra antes, solo en curvas finas.
constexpr float kThinRoundJointExcess = 0.3f;
constexpr float kRepairDiameter = 1.f;
// Cuadrado girado deja punta al ampliar; el disco mantiene el trazo fino.
constexpr float kRoundCapDiameter = 1.f;
// Bajo este grosor el parche recto es mas fiel que el circulo.
constexpr float kRoundCapMinThickness = 0.65f;
// Frena puntas finas de hasta 1 celda.
constexpr float kExposedCapGap = 0.65f;
// Bajo 7 grados la recta gana: cae en rejilla y funde con vecinos.
constexpr float kBoxTilt = 0.12f;
// Alcance de pareja de reparos: 4 baja picos sin romper diagonales de 2 celdas.
constexpr int kRepairReach = 4;
constexpr int kPadding = 2;
// Caja estirada quita sitio a fusiones de la ronda siguiente.
constexpr float kAbsorbSlack = 0.6f;
// Cada ronda remonta el dibujo entero; las ultimas apenas aportan.
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

// Grosor con cuadrado, no circulo, para separar zona maciza de trazo.
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

// Separa zona maciza (a rectangulos) de trazo (a tiras) del mismo color.
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

// 9 muestras del centro que deciden si pisa pixel ajeno.
constexpr float kCenterOffsets[3] = {0.4f, 0.5f, 0.6f};

// Franja subpixel libre; centro ajeno y area total limitados.
bool fitsPaintBoundary(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height,
    bool gridExact = true
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
        (gridExact ? kChainSpill : kSmoothSpill);
}

// Orla de 1 celda: abraza la mancha sin cruzar el dibujo.
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

// Orla no perdona vacio; sobre otro color sigue perdonada.
void maskVoid(
    std::vector<std::uint8_t>& mask,
    std::vector<std::uint8_t> const& empty
) {
    if (mask.size() != empty.size()) return;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (empty[i]) mask[i] = 0;
    }
}

// Derrame real: fuera de permitido Y de orla.
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

// Orla cuenta como casa solo para tiras cortas de cadena/banda/reparos.
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

// Orla solo perdona en tiras giradas; en recta exige pixel exacto.
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

// El circulo flota arriba: no puede quedar bajo otro color.
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
            if (placed.contains(x + 0.5f, y + 0.5f)) return true;
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

float directionDot(Point const& incoming, Point const& outgoing) {
    return std::clamp(incoming.x * outgoing.x + incoming.y * outgoing.y, -1.f, 1.f);
}

// Tope corto en silueta: el pincho saldria sobre el color vecino.
float miterExtension(float dot, float thickness, float limit) {
    limit = std::min(limit, thickness * 0.5f);
    if (dot <= 0.f) return limit;
    return std::min(limit, thickness * 0.5f * std::tan(std::acos(dot) * 0.5f));
}

// Disco solo si el pico del bisel asoma sobre lo que el disco taparia.
bool needsRoundJoint(float dot, float thickness, float excess = kRoundJointExcess) {
    if (thickness <= 0.f) return false;
    float const clamped = std::clamp(dot, -1.f, 1.f);
    if (clamped >= 1.f) return false;
    float const cosHalf =
        std::max(std::cos(std::acos(clamped) * 0.5f), 0.05f);
    return thickness * 0.5f * (1.f / cosHalf - 1.f) >= excess;
}

// Sin sitio para disco no se pone nada; el llamante alarga el trazo.
bool appendRoundCap(
    std::vector<Primitive>& output,
    Point const& position,
    float diameter,
    int color,
    int layer,
    int sourceWidth,
    int sourceHeight,
    std::vector<std::uint8_t> const& blocked
) {
    if (diameter < kRoundCapDiameter) return false;
    Primitive const cap{
        position.x,
        position.y,
        diameter,
        diameter,
        0.f,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Circle,
        static_cast<std::int16_t>(layer)
    };
    if (coversBlocked(cap, sourceWidth, sourceHeight, blocked)) return false;
    output.push_back(cap);
    return true;
}

struct Segment {
    Point direction;
    float length = 0.f;
};

// Grosor bajo el punto: camina hacia dentro hasta salir de la mancha.
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
    // En Suave arranca en la primera muestra pintada (max 1 celda).
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

// Mediana del grosor: marca hasta donde recortar al simplificar.
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

    // Pasarse compensa costura contra color, no contra vacio.
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
    // El exceso solo vale si tapa a lo largo de toda la tira.
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
        // Grosor local: el maximo global engordaba el trazo y lo pintaba dos veces.
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

    // Codos redondos como en cadena: disco donde el contorno dobla de verdad.
    std::vector<int> bandStartJoint(segments, -1);
    std::vector<int> bandEndJoint(segments, -1);
    std::vector<Primitive> bandDiscs;
    auto considerBandJoint = [&](std::size_t vertex, std::size_t previous,
                                 std::size_t next) {
        if (!geomValid[previous] || !geomValid[next]) return;
        float const diameter =
            std::max(geoms[previous].thickness, geoms[next].thickness);
        // Giros en tramos de 1-2 celdas son escalon, no esquina.
        if (measured[previous].length < 2.f || measured[next].length < 2.f) {
            return;
        }
        float const dot = directionDot(
            measured[previous].direction, measured[next].direction);
        float const jointExcess = diameter <= 1.6f
            ? kThinRoundJointExcess : kRoundJointExcess;
        if (!needsRoundJoint(dot, diameter, jointExcess)) {
            return;
        }
        auto const& before = geoms[previous];
        auto const& after = geoms[next];
        // Si el pico cae en permitido el inglete basta y el disco sobra.
        {
            float const prevExtent =
                miterExtension(dot, before.thickness, kBandMiter);
            float const nextExtent =
                miterExtension(dot, after.thickness, kBandMiter);
            float const a0x = loop[vertex].x +
                before.inwardX * (before.offset - before.thickness * 0.5f);
            float const a0y = loop[vertex].y +
                before.inwardY * (before.offset - before.thickness * 0.5f);
            float const a1x = loop[vertex].x +
                after.inwardX * (after.offset - after.thickness * 0.5f);
            float const a1y = loop[vertex].y +
                after.inwardY * (after.offset - after.thickness * 0.5f);
            Point const& d0 = measured[previous].direction;
            Point const& d1 = measured[next].direction;
            float const c0x = a0x + d0.x * prevExtent;
            float const c0y = a0y + d0.y * prevExtent;
            float const c1x = a1x - d1.x * nextExtent;
            float const c1y = a1y - d1.y * nextExtent;
            float tipX = (c0x + c1x) * 0.5f;
            float tipY = (c0y + c1y) * 0.5f;
            float const cross = d0.x * d1.y - d0.y * d1.x;
            if (std::abs(cross) >= 1e-4f) {
                float const s =
                    ((a1x - a0x) * d1.y - (a1y - a0y) * d1.x) / cross;
                tipX = a0x + d0.x * s;
                tipY = a0y + d0.y * s;
            }
            auto insidePermitted = [&](float x, float y) {
                int const cellX =
                    static_cast<int>(std::floor(x)) + region.offsetX;
                int const cellY =
                    static_cast<int>(std::floor(y)) + region.offsetY;
                if (cellX < 0 || cellY < 0 || cellX >= sourceWidth ||
                    cellY >= sourceHeight) {
                    return true;
                }
                if (permitted.size() !=
                    static_cast<std::size_t>(sourceWidth) * sourceHeight) {
                    return false;
                }
                return permitted[static_cast<std::size_t>(cellY) * sourceWidth +
                                 cellX] != 0;
            };
            if (insidePermitted(c0x, c0y) && insidePermitted(c1x, c1y) &&
                insidePermitted(tipX, tipY)) {
                return;
            }
        }
        float inwardX = before.inwardX + after.inwardX;
        float inwardY = before.inwardY + after.inwardY;
        float const inwardLength = std::hypot(inwardX, inwardY);
        float const offset = (before.offset + after.offset) * 0.5f;
        Point center = loop[vertex];
        if (inwardLength > 0.01f) {
            center.x += inwardX / inwardLength * offset;
            center.y += inwardY / inwardLength * offset;
        }
        for (std::size_t index = 0; index < bandDiscs.size(); ++index) {
            auto const& known = bandDiscs[index];
            if (std::hypot(
                    known.x - center.x - static_cast<float>(region.offsetX),
                    known.y - center.y - static_cast<float>(region.offsetY)) <
                std::min(known.width, diameter) * 0.5f) {
                float const grown = std::max(known.width, diameter);
                bandDiscs[index].width = grown;
                bandDiscs[index].height = grown;
                bandEndJoint[previous] = static_cast<int>(index);
                bandStartJoint[next] = static_cast<int>(index);
                return;
            }
        }
        Primitive const cap{
            center.x + static_cast<float>(region.offsetX),
            center.y + static_cast<float>(region.offsetY),
            diameter, diameter, 0.f,
            static_cast<std::uint16_t>(color),
            PrimitiveKind::Circle, static_cast<std::int16_t>(layer)
        };
        if (coversBlocked(cap, sourceWidth, sourceHeight, blocked)) return;
        if (!fitsPaintBoundary(cap, permitted, sourceWidth, sourceHeight)) return;
        int const index = static_cast<int>(bandDiscs.size());
        bandDiscs.push_back(cap);
        bandEndJoint[previous] = index;
        bandStartJoint[next] = index;
    };
    if (contour.closed) {
        for (std::size_t vertex = 0; vertex < loop.size(); ++vertex) {
            considerBandJoint(
                vertex, (vertex + segments - 1) % segments, vertex % segments);
        }
    } else if (loop.size() > 2) {
        for (std::size_t vertex = 1; vertex + 1 < loop.size(); ++vertex) {
            considerBandJoint(vertex, vertex - 1, vertex);
        }
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

        bool const hasPrevious = contour.closed || i > 0;
        bool const hasNext = contour.closed || i + 1 < segments;
        float const startDot = hasPrevious
            ? directionDot(measured[(i + segments - 1) % segments].direction, segment.direction)
            : 1.f;
        float const endDot = hasNext
            ? directionDot(segment.direction, measured[(i + 1) % segments].direction)
            : 1.f;
        float const startExtent = bandStartJoint[i] >= 0
            ? 0.f
            : hasPrevious
            ? miterExtension(startDot, thickness, kBandMiter) : thickness * 0.5f;
        float const endExtent = bandEndJoint[i] >= 0
            ? 0.f
            : hasNext
            ? miterExtension(endDot, thickness, kBandMiter) : thickness * 0.5f;

        float const shift = (endExtent - startExtent) * 0.5f;
        // El exceso nunca llega a medio grosor en linea fina.
        float const offset = geoms[i].offset;
        output.push_back({
            midX + segment.direction.x * shift + inwardX * offset +
                static_cast<float>(region.offsetX),
            midY + segment.direction.y * shift + inwardY * offset +
                static_cast<float>(region.offsetY),
            segment.length + startExtent + endExtent,
            thickness,
            std::atan2(segment.direction.y, segment.direction.x) * 180.f / kPi,
            static_cast<std::uint16_t>(color),
            PrimitiveKind::Stroke,
            static_cast<std::int16_t>(layer)
        });

    }
    output.insert(output.end(), bandDiscs.begin(), bandDiscs.end());
}

// Sin trazo no dibuja nada: el llamante usa el camino del contorno.
bool appendChain(
    std::vector<Primitive>& output,
    Region const& region,
    std::vector<int> const& component,
    int sourceWidth,
    int sourceHeight,
    float radius,
    int color,
    int layer,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true
) {
    auto const skeleton = thin(component, sourceWidth);
    auto const paths = skeletonPaths(skeleton);
    // Eje limitado a medio grosor: si no, la tira cruza el dibujo.
    float const tolerance = std::clamp(radius * 0.5f, 0.6f, 1.3f);

    struct Line {
        std::vector<Point> points;
        std::array<bool, 2> joined{};
        std::array<bool, 2> terminal{};
        // -1 = giro desconocido: remate cuadrado de medio grosor.
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

        std::vector<Point> centered = points;
        for (std::size_t i = 0; i < points.size(); ++i) {
            Point const prev = (i > 0) ? points[i - 1] : points[i];
            Point const next = (i + 1 < points.size()) ? points[i + 1] : points[i];
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

        auto reduced = simplify(centered, tolerance);
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
    // Suave relaja el vacio; Pixel no perdona.
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

    // Eje corto = mancha compacta: va por contorno, no por tira.
    if (span < nominal * kChainSlenderness) return false;

    // Tiras aparte: el eje puede atajar por donde no hay color.
    std::vector<Primitive> strokes;

    // Punta libre: disco si cabe, si no alarga medio grosor.
    auto terminalExtension = [&](Point const& point, float thickness) {
        if (appendRoundCap(
                strokes, point, thickness, color, layer,
                sourceWidth, sourceHeight, blocked)) {
            return 0.f;
        }
        return thickness * 0.5f;
    };

    // Grosor previo para decidir que codos llevan disco.
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

    // Codo redondo: disco si cabe sin tapar otro color; si no, bisel.
    std::vector<std::vector<int>> startJoint(lines.size());
    std::vector<std::vector<int>> endJoint(lines.size());
    for (std::size_t slot = 0; slot < lines.size(); ++slot) {
        std::size_t const segments = lines[slot].points.size() - 1;
        startJoint[slot].assign(segments, -1);
        endJoint[slot].assign(segments, -1);
    }
    struct JointBid {
        std::size_t line = 0;
        std::size_t segment = 0;
        bool start = false;
    };
    std::vector<Point> jointPoints;
    std::vector<float> jointDiameters;
    std::vector<std::vector<JointBid>> jointBids;
    auto bidJoint = [&](Point point, float diameter, std::size_t line,
                        std::size_t segment, bool start) {
        for (std::size_t index = 0; index < jointPoints.size(); ++index) {
            if (pointDistance(jointPoints[index], point) <
                std::min(jointDiameters[index], diameter) * 0.5f) {
                jointDiameters[index] = std::max(jointDiameters[index], diameter);
                jointBids[index].push_back({line, segment, start});
                return;
            }
        }
        jointPoints.push_back(point);
        jointDiameters.push_back(diameter);
        jointBids.push_back({{line, segment, start}});
    };
    for (std::size_t slot = 0; slot < lines.size(); ++slot) {
        auto const& line = lines[slot];
        auto const& reduced = line.points;
        std::size_t const segments = reduced.size() - 1;
        auto const measured = measure(reduced, segments);
        for (std::size_t vertex = 1; vertex + 1 < reduced.size(); ++vertex) {
            if (measured[vertex - 1].length <= 0.05f ||
                measured[vertex].length <= 0.05f) {
                continue;
            }
            float const diameter = std::max(
                thicknessOf[slot][vertex - 1], thicknessOf[slot][vertex]);
            float const jointExcess = diameter <= 1.6f
                ? kThinRoundJointExcess : kRoundJointExcess;
            if (!needsRoundJoint(
                    directionDot(
                        measured[vertex - 1].direction,
                        measured[vertex].direction),
                    diameter, jointExcess)) {
                continue;
            }
            bidJoint(reduced[vertex], diameter, slot, vertex - 1, false);
            bidJoint(reduced[vertex], diameter, slot, vertex, true);
        }
        for (int end = 0; end < 2; ++end) {
            if (!line.joined[static_cast<std::size_t>(end)]) continue;
            std::size_t const segment = end ? segments - 1 : 0;
            std::size_t const vertex = end ? reduced.size() - 1 : 0;
            // -1 es continuacion suave: el remate cuadrado ya la tapa.
            float const known =
                line.jointDot[static_cast<std::size_t>(end)];
            if (known < -0.999f) continue;
            float const jointExcess = thicknessOf[slot][segment] <= 1.6f
                ? kThinRoundJointExcess : kRoundJointExcess;
            if (!needsRoundJoint(known, thicknessOf[slot][segment], jointExcess)) {
                continue;
            }
            bidJoint(
                reduced[vertex], thicknessOf[slot][segment], slot, segment,
                end == 0);
        }
    }
    std::vector<std::uint8_t> jointKept(jointPoints.size(), 0);
    for (std::size_t index = 0; index < jointPoints.size(); ++index) {
        Primitive const cap{
            jointPoints[index].x, jointPoints[index].y,
            jointDiameters[index], jointDiameters[index],
            0.f, static_cast<std::uint16_t>(color),
            PrimitiveKind::Circle, static_cast<std::int16_t>(layer)
        };
        if (coversBlocked(cap, sourceWidth, sourceHeight, blocked)) continue;
        if (shapeFarSpill(cap, permitted, nearMask, sourceWidth, sourceHeight) >
            kChainSpill) {
            continue;
        }
        jointKept[index] = 1;
        for (auto const& bid : jointBids[index]) {
            (bid.start ? startJoint[bid.line][bid.segment]
                       : endJoint[bid.line][bid.segment]) =
                static_cast<int>(index);
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
            // Grosor por lo bajo para no salirse donde estrecha.
            float const thickness = thicknessOf[slot][i];
            // En codo redondo la tira corta a tope; en recto sigue el bisel.
            float const miter = thickness * 0.5f;
            float const startExtension = startJoint[slot][i] >= 0
                ? 0.f
                : i > 0
                ? miterExtension(
                      directionDot(measured[i - 1].direction, segment.direction),
                      thickness, miter)
                : line.joined[0]
                    ? miterExtension(line.jointDot[0], thickness, miter)
                    : terminalExtension(first, thickness);
            float const endExtension = endJoint[slot][i] >= 0
                ? 0.f
                : i + 1 < segments
                ? miterExtension(
                      directionDot(segment.direction, measured[i + 1].direction),
                      thickness, miter)
                : line.joined[1]
                    ? miterExtension(line.jointDot[1], thickness, miter)
                    : terminalExtension(second, thickness);
            float const shift = (endExtension - startExtension) * 0.5f;
            strokes.push_back({
                midX + segment.direction.x * shift,
                midY + segment.direction.y * shift,
                segment.length + startExtension + endExtension,
                thickness,
                std::atan2(segment.direction.y, segment.direction.x) * 180.f / kPi,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Stroke,
                static_cast<std::int16_t>(layer)
            });
        }
    }
    for (std::size_t index = 0; index < jointPoints.size(); ++index) {
        if (!jointKept[index]) continue;
        strokes.push_back({
            jointPoints[index].x, jointPoints[index].y,
            jointDiameters[index], jointDiameters[index],
            0.f, static_cast<std::uint16_t>(color),
            PrimitiveKind::Circle, static_cast<std::int16_t>(layer)
        });
    }
    if (strokes.empty()) return false;
    // Caen tiras sueltas, no la cadena: el parche recoge lo sin tapar.
    strokes.erase(std::remove_if(strokes.begin(), strokes.end(),
        [&](Primitive const& stroke) {
            return shapeFarSpill(
                       stroke, permitted, nearMask, sourceWidth, sourceHeight) >
                kChainEraseSpill;
        }), strokes.end());
    if (strokes.empty()) return false;
    output.insert(output.end(), strokes.begin(), strokes.end());
    return true;
}

// El circulo flota arriba: solo vale si nada pinta encima.
bool appendCircle(
    std::vector<Primitive>& output,
    Region const& region,
    int color,
    int layer,
    int sourceWidth,
    int sourceHeight,
    std::vector<std::uint8_t> const& blocked
) {
    float const boxWidth = static_cast<float>(region.width - kPadding * 2);
    float const boxHeight = static_cast<float>(region.height - kPadding * 2);
    if (boxWidth < 4.f || boxHeight < 4.f) return false;
    float const aspect = std::max(boxWidth, boxHeight) / std::min(boxWidth, boxHeight);
    if (aspect > 1.8f) return false;

    float sumX = 0.f;
    float sumY = 0.f;
    int filledCount = 0;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            if (region.filled(x, y)) {
                sumX += static_cast<float>(x + region.offsetX) + 0.5f;
                sumY += static_cast<float>(y + region.offsetY) + 0.5f;
                ++filledCount;
            }
        }
    }
    float const cX = filledCount > 0
        ? sumX / static_cast<float>(filledCount)
        : static_cast<float>(region.offsetX) + kPadding + boxWidth * 0.5f;
    float const cY = filledCount > 0
        ? sumY / static_cast<float>(filledCount)
        : static_cast<float>(region.offsetY) + kPadding + boxHeight * 0.5f;

    bool const hasBlocked =
        blocked.size() == static_cast<std::size_t>(sourceWidth) * sourceHeight;
    // Cuantizar mueve el borde casi 1 celda: se busca en una vecindad.
    struct Fit {
        Primitive shape;
        int covered = 0;
        int missing = 0;
        int spilled = 0;
        float score = std::numeric_limits<float>::lowest();
    };

    auto evaluate = [&](Primitive const& candidate) -> std::optional<Fit> {
        auto const placed = xformOf(candidate);
        auto const box = xformBox(placed, sourceWidth, sourceHeight);
        if (box[2] < box[0] || box[3] < box[1]) return std::nullopt;
        int covered = 0;
        int missing = 0;
        int spilled = 0;
        for (int y = 0; y < region.height; ++y) {
            for (int x = 0; x < region.width; ++x) {
                float const sampleX = static_cast<float>(x + region.offsetX) + 0.5f;
                float const sampleY = static_cast<float>(y + region.offsetY) + 0.5f;
                if (placed.contains(sampleX, sampleY)) {
                    if (region.filled(x, y)) ++covered;
                    else ++spilled;
                } else if (region.filled(x, y)) {
                    ++missing;
                }
            }
        }
        if (hasBlocked) {
            for (int y = box[1]; y <= box[3]; ++y) {
                for (int x = box[0]; x <= box[2]; ++x) {
                    auto const index = static_cast<std::size_t>(y) * sourceWidth + x;
                    if (blocked[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                        return std::nullopt;
                    }
                }
            }
        }
        int const target = std::max(1, filledCount);
        float const tolerance = std::max(1.f, static_cast<float>(target) * 0.10f);
        if (covered < static_cast<int>(std::ceil(target * 0.92f)) ||
            static_cast<float>(missing) > tolerance ||
            static_cast<float>(spilled) > tolerance) {
            return std::nullopt;
        }
        Fit fit;
        fit.shape = candidate;
        fit.covered = covered;
        fit.missing = missing;
        fit.spilled = spilled;
        // Puntua cobertura ajustada; el area evita elipses grandes.
        float const candidateArea = candidate.width * candidate.height;
        fit.score = static_cast<float>(covered) - static_cast<float>(spilled) * 1.35f -
            candidateArea * 0.006f;
        return fit;
    };

    Primitive const exact{
        cX, cY, boxWidth, boxHeight, 0.f,
        static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
        static_cast<std::int16_t>(layer)
    };
    if (auto fit = evaluate(exact)) {
        // Mantiene el ajuste estricto cuando ya acierta.
        float const strictLimit = static_cast<float>(std::max(1, filledCount)) * 0.10f;
        if (static_cast<float>(fit->missing) <= strictLimit &&
            static_cast<float>(fit->spilled) <= strictLimit) {
            output.push_back(exact);
            return true;
        }
    }

    float const boundsCenterX = static_cast<float>(region.offsetX + kPadding) + boxWidth * 0.5f;
    float const boundsCenterY = static_cast<float>(region.offsetY + kPadding) + boxHeight * 0.5f;
    constexpr std::array<std::pair<float, float>, 9> kScalePairs{{
        {0.88f, 0.88f}, {0.94f, 0.94f}, {1.f, 1.f},
        {1.06f, 1.06f}, {1.12f, 1.12f},
        {0.94f, 1.06f}, {1.06f, 0.94f},
        {0.88f, 1.12f}, {1.12f, 0.88f}
    }};
    constexpr std::array<float, 3> kOffsets{-0.35f, 0.f, 0.35f};
    Fit best;
    bool found = false;
    for (float centerBaseX : {cX, boundsCenterX}) {
        for (float centerBaseY : {cY, boundsCenterY}) {
            for (float dx : kOffsets) {
                for (float dy : kOffsets) {
                    for (auto const [widthScale, heightScale] : kScalePairs) {
                        float const candidateWidth = boxWidth * widthScale;
                        float const candidateHeight = boxHeight * heightScale;
                        float const candidateAspect =
                            std::max(candidateWidth, candidateHeight) /
                            std::min(candidateWidth, candidateHeight);
                        if (candidateAspect > 1.8f) continue;
                        Primitive const candidate{
                            centerBaseX + dx, centerBaseY + dy,
                            candidateWidth, candidateHeight, 0.f,
                            static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
                            static_cast<std::int16_t>(layer)
                        };
                        auto fit = evaluate(candidate);
                        if (!fit || (found && fit->score <= best.score)) continue;
                        best = *fit;
                        found = true;
                    }
                }
            }
        }
    }
    if (!found) return false;
    output.push_back(best.shape);
    return true;
}

float turn(Point const& origin, Point const& first, Point const& second) {
    return (first.x - origin.x) * (second.y - origin.y) -
        (first.y - origin.y) * (second.x - origin.x);
}

std::vector<Point> convexHull(std::vector<int> const& positions, int width) {
    std::vector<Point> points;
    points.reserve(positions.size() * 4);
    for (int position : positions) {
        float const x = static_cast<float>(position % width);
        float const y = static_cast<float>(position / width);
        points.push_back({x, y});
        points.push_back({x + 1.f, y});
        points.push_back({x, y + 1.f});
        points.push_back({x + 1.f, y + 1.f});
    }
    std::sort(points.begin(), points.end(), [](Point const& left, Point const& right) {
        return left.x < right.x || (left.x == right.x && left.y < right.y);
    });
    points.erase(std::unique(points.begin(), points.end(), [](Point const& left, Point const& right) {
        return left.x == right.x && left.y == right.y;
    }), points.end());
    if (points.size() <= 3) return points;

    std::vector<Point> hull(points.size() * 2);
    std::size_t count = 0;
    for (auto const& point : points) {
        while (count >= 2 && turn(hull[count - 2], hull[count - 1], point) <= 0.f) --count;
        hull[count++] = point;
    }
    std::size_t const lower = count + 1;
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
        while (count >= lower && turn(hull[count - 2], hull[count - 1], *it) <= 0.f) --count;
        hull[count++] = *it;
    }
    hull.resize(count > 1 ? count - 1 : count);
    return hull;
}

Primitive rightTriangle(
    Point right,
    Point first,
    Point second,
    int color,
    int layer
) {
    Point firstLeg{first.x - right.x, first.y - right.y};
    Point secondLeg{second.x - right.x, second.y - right.y};
    if (firstLeg.x * secondLeg.y - firstLeg.y * secondLeg.x < 0.f) {
        std::swap(firstLeg, secondLeg);
    }
    float const width = std::hypot(firstLeg.x, firstLeg.y);
    float const height = std::hypot(secondLeg.x, secondLeg.y);
    return {
        right.x + (firstLeg.x + secondLeg.x) * 0.5f,
        right.y + (firstLeg.y + secondLeg.y) * 0.5f,
        width,
        height,
        std::atan2(firstLeg.y, firstLeg.x) * 180.f / kPi,
        static_cast<std::uint16_t>(color),
        width / std::max(height, 0.01f) >= 1.5f
            ? PrimitiveKind::WideTriangle : PrimitiveKind::Triangle,
        static_cast<std::int16_t>(layer)
    };
}

std::vector<Primitive> splitTriangle(
    Point first,
    Point second,
    Point third,
    int color,
    int layer
) {
    std::array<std::pair<Point, Point>, 3> sides{{
        {first, second}, {second, third}, {third, first}
    }};
    auto longest = std::max_element(sides.begin(), sides.end(), [](auto const& left, auto const& right) {
        return pointDistance(left.first, left.second) < pointDistance(right.first, right.second);
    });
    Point const baseStart = longest->first;
    Point const baseEnd = longest->second;
    Point const tip = longest == sides.begin() ? third
        : longest == sides.begin() + 1 ? first : second;
    float const dx = baseEnd.x - baseStart.x;
    float const dy = baseEnd.y - baseStart.y;
    float const lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.01f) return {};
    float const projection = std::clamp(
        ((tip.x - baseStart.x) * dx + (tip.y - baseStart.y) * dy) / lengthSq,
        0.f, 1.f);
    Point const foot{baseStart.x + dx * projection, baseStart.y + dy * projection};

    std::vector<Primitive> shapes;
    if (pointDistance(baseStart, foot) > 0.05f) {
        shapes.push_back(rightTriangle(foot, baseStart, tip, color, layer));
    }
    if (pointDistance(foot, baseEnd) > 0.05f) {
        shapes.push_back(rightTriangle(foot, tip, baseEnd, color, layer));
    }
    return shapes;
}

float fitSimilarity(
    std::vector<int> const& positions,
    std::vector<std::uint8_t> const& target,
    int width,
    int height,
    std::vector<Primitive> const& shapes,
    std::vector<std::uint8_t> const& blocked
) {
    auto const placed = xformsOf(shapes);
    auto covered = [&](float x, float y) {
        return std::any_of(placed.begin(), placed.end(), [&](ShapeXform const& shape) {
            return shape.contains(x, y);
        });
    };

    int correct = 0;
    for (int position : positions) {
        if (covered(
                static_cast<float>(position % width) + 0.5f,
                static_cast<float>(position / width) + 0.5f)) {
            ++correct;
        }
    }

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (auto const& shape : shapes) {
        float const angle = shape.rotation * kPi / 180.f;
        float const extentX = std::abs(std::cos(angle)) * shape.width * 0.5f +
            std::abs(std::sin(angle)) * shape.height * 0.5f;
        float const extentY = std::abs(std::sin(angle)) * shape.width * 0.5f +
            std::abs(std::cos(angle)) * shape.height * 0.5f;
        minX = std::min(minX, std::max(0, static_cast<int>(std::floor(shape.x - extentX))));
        minY = std::min(minY, std::max(0, static_cast<int>(std::floor(shape.y - extentY))));
        maxX = std::max(maxX, std::min(width - 1, static_cast<int>(std::ceil(shape.x + extentX))));
        maxY = std::max(maxY, std::min(height - 1, static_cast<int>(std::ceil(shape.y + extentY))));
    }

    // Asomar sobre capa tapada cuesta menos; orla perdona en trazo largo y fino.
    int pieceMinX = width;
    int pieceMinY = height;
    int pieceMaxX = -1;
    int pieceMaxY = -1;
    for (int position : positions) {
        int const x = position % width;
        int const y = position / width;
        pieceMinX = std::min(pieceMinX, x);
        pieceMaxX = std::max(pieceMaxX, x);
        pieceMinY = std::min(pieceMinY, y);
        pieceMaxY = std::max(pieceMaxY, y);
    }
    int const longSpan = std::max(
        pieceMaxX - pieceMinX + 1, pieceMaxY - pieceMinY + 1);
    bool const thinPiece = longSpan > 0 &&
        static_cast<float>(positions.size()) / static_cast<float>(longSpan) <=
            static_cast<float>(kThickSpan) &&
        longSpan >= kLongSpan;
    float spilled = 0.f;
    bool const hasBlocked = blocked.size() == target.size();
    std::vector<std::uint8_t> nearMask;
    if (thinPiece) nearMask = nearCells(positions, width, height);
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if (target[index] || !covered(x + 0.5f, y + 0.5f)) continue;
            if (thinPiece && index < nearMask.size() && nearMask[index]) continue;
            spilled += hasBlocked && blocked[index] ? kCoveredSpill : 1.f;
        }
    }
    float const unionArea = static_cast<float>(positions.size()) + spilled;
    return unionArea > 0.f ? static_cast<float>(correct) / unionArea : 0.f;
}

bool appendCapsule(
    std::vector<Primitive>& output,
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& empty
) {
    if (positions.size() < 6) return false;
    std::vector<std::uint8_t> target(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) target[static_cast<std::size_t>(position)] = 1;
    // La mancha y el hueco: lo unico contra lo que un borde recto puede partir.
    std::vector<std::uint8_t> unclaimed = target;
    if (empty.size() == unclaimed.size()) {
        for (std::size_t i = 0; i < unclaimed.size(); ++i) unclaimed[i] |= empty[i];
    }
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
    float const principal = 0.5f * std::atan2(2.f * xy, xx - yy);
    constexpr std::array<float, 4> kLengthPadding{0.4f, 0.6f, 0.8f, 1.f};
    constexpr std::array<float, 4> kWidthPadding{0.f, 0.2f, 0.4f, 0.6f};
    float bestSimilarity = 0.f;
    std::vector<Primitive> best;
    auto consider = [&](std::vector<Primitive> const& shapes, bool tight) {
        float const similarity = fitSimilarity(
            positions, target, width, height, shapes, blocked);
        if (similarity <= bestSimilarity) return;
        if (tight && std::any_of(shapes.begin(), shapes.end(),
                                 [&](Primitive const& shape) {
                                     return !fitsPaintBoundary(
                                         shape, unclaimed, width, height);
                                 })) {
            return;
        }
        bestSimilarity = similarity;
        best = shapes;
    };
    // Eje de inercia no basta en manchas simetricas: suma giros de la envolvente.
    std::vector<float> angles;
    for (int offset = -6; offset <= 6; ++offset) {
        angles.push_back(principal + offset * kPi / 180.f);
    }
    auto hull = convexHull(positions, width);
    for (float tolerance = 0.5f; hull.size() > 12 && tolerance <= 2.f; tolerance += 0.5f) {
        hull = simplifyLoop(hull, tolerance);
    }
    for (std::size_t i = 0; i < hull.size() && hull.size() >= 3; ++i) {
        auto const& first = hull[i];
        auto const& second = hull[(i + 1) % hull.size()];
        float const edge = std::atan2(second.y - first.y, second.x - first.x);
        // Rectangulo cada 90 grados: no probar giros duplicados.
        float folded = std::fmod(edge, kPi * 0.5f);
        if (folded < 0.f) folded += kPi * 0.5f;
        if (std::none_of(angles.begin(), angles.end(), [&](float known) {
                float difference = std::fmod(std::abs(known - folded), kPi * 0.5f);
                difference = std::min(difference, kPi * 0.5f - difference);
                return difference < 0.008f;
            })) {
            angles.push_back(folded);
        }
    }

    for (float angle : angles) {
        float const cosine = std::cos(angle);
        float const sine = std::sin(angle);
        float const quarter = std::fmod(std::abs(angle), kPi * 0.5f);
        bool const upright = std::min(quarter, kPi * 0.5f - quarter) < kBoxTilt;
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
        float const support = 0.5f * (std::abs(cosine) + std::abs(sine));
        for (float lengthPadding : kLengthPadding) {
            for (float widthPadding : kWidthPadding) {
                float const totalLength = maxMajor - minMajor + support * 2.f * lengthPadding;
                float const diameter = maxMinor - minMinor + support * 2.f * widthPadding;
                // Capsula exige eje largo; el rectangulo salva al rombo.
                bool const slender = totalLength / std::max(diameter, 0.01f) >= 1.6f;
                float const lineLength = std::max(totalLength - diameter, 0.05f);
                float const middleMajor = (minMajor + maxMajor) * 0.5f;
                float const middleMinor = (minMinor + maxMinor) * 0.5f;
                Point const center{
                    middleMajor * cosine - middleMinor * sine,
                    middleMajor * sine + middleMinor * cosine
                };
                Point const extent{
                    cosine * lineLength * 0.5f,
                    sine * lineLength * 0.5f
                };
                // Rectangulo primero: si empata gana por usar menos objetos.
                std::vector<Primitive> squared{{
                    center.x, center.y, totalLength, diameter,
                    angle * 180.f / kPi, static_cast<std::uint16_t>(color),
                    PrimitiveKind::Stroke, static_cast<std::int16_t>(layer)
                }};
                std::vector<Primitive> rounded{{
                    center.x, center.y, lineLength, diameter,
                    angle * 180.f / kPi, static_cast<std::uint16_t>(color),
                    PrimitiveKind::Stroke, static_cast<std::int16_t>(layer)
                }};
                // Casi recto va por bloques: borde limpio y fundible.
                if (slender || !upright) consider(squared, !slender);
                if (!slender) continue;
                bool const capped = appendRoundCap(
                        rounded, {center.x - extent.x, center.y - extent.y},
                        diameter, color, layer, width, height, blocked) &&
                    appendRoundCap(
                        rounded, {center.x + extent.x, center.y + extent.y},
                        diameter, color, layer, width, height, blocked);
                if (capped) consider(rounded, false);
            }
        }
    }
    if (bestSimilarity < 0.97f) return false;
    output.insert(output.end(), best.begin(), best.end());
    return true;
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
    std::vector<std::uint8_t> target(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) target[static_cast<std::size_t>(position)] = 1;
    auto hull = convexHull(positions, width);
    for (float tolerance = 0.35f; hull.size() > 16 && tolerance <= 1.4f; tolerance += 0.35f) {
        hull = simplifyLoop(hull, tolerance);
    }
    if (hull.size() < 3 || hull.size() > 16) return false;

    float bestSimilarity = 0.f;
    std::vector<Primitive> best;
    for (std::size_t first = 0; first + 2 < hull.size(); ++first) {
        for (std::size_t second = first + 1; second + 1 < hull.size(); ++second) {
            for (std::size_t third = second + 1; third < hull.size(); ++third) {
                auto shapes = splitTriangle(
                    hull[first], hull[second], hull[third], color, layer);
                if (shapes.empty()) continue;
                float const similarity = fitSimilarity(
                    positions, target, width, height, shapes, blocked);
                if (similarity <= bestSimilarity) continue;
                bestSimilarity = similarity;
                best = std::move(shapes);
            }
        }
    }
    if (bestSimilarity < 0.88f) return false;
    output.insert(output.end(), best.begin(), best.end());
    return true;
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
        // Suave: basta media celda; 16/16 dejaba bloques sueltos en diagonales.
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
    std::vector<std::uint8_t> const& blocked,
    bool gridExact = true
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
    float const angle = 0.5f * std::atan2(2.f * xy, xx - yy);
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
    // Caja girada de ele/ese arrasa huecos: limita su sobrante.
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
    // Mota redonda va como ovalo si no queda bajo otro color.
    if (std::max(halfWidth, halfHeight) <= std::min(halfWidth, halfHeight) * 1.25f) {
        float scale = 1.f;
        for (int position : positions) {
            float const dx = static_cast<float>(position % width) + 0.5f - center.x;
            float const dy = static_cast<float>(position / width) + 0.5f - center.y;
            float const localX = dx * cosine + dy * sine;
            float const localY = -dx * sine + dy * cosine;
            scale = std::max(scale, std::sqrt(
                localX * localX / (halfWidth * halfWidth) +
                localY * localY / (halfHeight * halfHeight)));
        }
        Primitive const round{
            center.x, center.y, halfWidth * scale * 2.f, halfHeight * scale * 2.f,
            angle * 180.f / kPi, static_cast<std::uint16_t>(color),
            PrimitiveKind::Circle,
            static_cast<std::int16_t>(layer)
        };
        if (!coversBlocked(round, width, height, blocked) &&
            fitsPaintBoundary(round, permitted, width, height, gridExact)) {
            output.push_back(round);
            return true;
        }
    }

    if (!fitsPaintBoundary(patch, permitted, width, height, gridExact)) return false;
    output.push_back(patch);
    return true;
}

Primitive repairStroke(
    int first,
    int second,
    int width,
    int color,
    int layer
) {
    float const x0 = static_cast<float>(first % width) + 0.5f;
    float const y0 = static_cast<float>(first / width) + 0.5f;
    float const x1 = static_cast<float>(second % width) + 0.5f;
    float const y1 = static_cast<float>(second / width) + 0.5f;
    float const dx = x1 - x0;
    float const dy = y1 - y0;
    return {
        (x0 + x1) * 0.5f,
        (y0 + y1) * 0.5f,
        std::hypot(dx, dy) + kRepairDiameter,
        kRepairDiameter,
        std::atan2(dy, dx) * 180.f / kPi,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Stroke,
        static_cast<std::int16_t>(layer)
    };
}

int coveredRepairs(
    Primitive const& object,
    std::vector<std::uint8_t> const& remaining,
    int width,
    int height
) {
    auto const placed = xformOf(object);
    auto const box = xformBox(placed, width, height);
    if (box[2] < box[0] || box[3] < box[1]) return 0;
    int count = 0;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            int const position = y * width + x;
            if (remaining[static_cast<std::size_t>(position)] &&
                placed.contains(x + 0.5f, y + 0.5f)) {
                ++count;
            }
        }
    }
    return count;
}

void consumeRepairs(
    Primitive const& object,
    std::vector<std::uint8_t>& remaining,
    int width,
    int height
) {
    auto const placed = xformOf(object);
    auto const box = xformBox(placed, width, height);
    if (box[2] < box[0] || box[3] < box[1]) return;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            int const position = y * width + x;
            if (remaining[static_cast<std::size_t>(position)] &&
                placed.contains(x + 0.5f, y + 0.5f)) {
                remaining[static_cast<std::size_t>(position)] = 0;
            }
        }
    }
}

constexpr int kPruneScale = 8;

struct PruneEntry {
    Primitive const* object = nullptr;
    std::uint8_t* keep = nullptr;
};

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

// Sobra lo que no cambia el dibujo, de abajo arriba y de arriba abajo.
void markUsefulObjects(std::vector<PruneEntry> entries, int width, int height) {
    std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right) {
        return left.object->layer < right.object->layer;
    });
    std::vector<ShapeXform> forms;
    forms.reserve(entries.size());
    for (auto const& entry : entries) forms.push_back(xformOf(*entry.object));
    std::size_t const samples =
        static_cast<std::size_t>(width) * height * kPruneScale * kPruneScale;

    // Aportes menores de media celda en borde se pueden quitar; nunca un centro.
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    std::vector<std::int16_t> expected(cells, -1);
    std::vector<std::int16_t> centers(cells, -1);
    auto visitCenters = [&](ShapeXform const& shape, auto visit) {
        auto const box = xformBox(shape, width, height);
        for (int y = box[1]; y <= box[3]; ++y) {
            for (int x = box[0]; x <= box[2]; ++x) {
                if (shape.contains(x + 0.5f, y + 0.5f)) {
                    visit(static_cast<std::size_t>(y) * width + x);
                }
            }
        }
    };
    for (std::size_t index = 0; index < entries.size(); ++index) {
        visitCenters(forms[index], [&](std::size_t cell) {
            expected[cell] =
                static_cast<std::int16_t>(entries[index].object->color);
        });
    }
    std::vector<std::uint8_t> interior(cells, 0);
    for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
            auto const cell = static_cast<std::size_t>(y) * width + x;
            auto const color = expected[cell];
            interior[cell] = color >= 0 && expected[cell - 1] == color &&
                expected[cell + 1] == color && expected[cell - width] == color &&
                expected[cell + width] == color;
        }
    }
    std::vector<std::uint8_t> discarded(cells, 0);
    std::vector<std::int16_t> painted(samples, -1);
    std::vector<std::uint8_t> useful(entries.size(), 0);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto const& object = *entries[index].object;
        auto const color = static_cast<std::int16_t>(object.color);
        if (!anySample(forms[index], width, height, [&](std::size_t sample) {
                return painted[sample] != color;
            })) {
            continue;
        }
        bool needsCenter = false;
        visitCenters(forms[index], [&](std::size_t cell) {
            needsCenter |= centers[cell] != color;
        });
        if (!needsCenter) {
            std::map<std::size_t, int> contribution;
            bool significant = false;
            anySample(forms[index], width, height, [&](std::size_t sample) {
                if (painted[sample] == color) return false;
                auto const x = sample % (width * kPruneScale) / kPruneScale;
                auto const y = sample / (width * kPruneScale) / kPruneScale;
                auto const cell = y * width + x;
                significant |= interior[cell] ||
                    ++contribution[cell] + discarded[cell] > kPruneScale * kPruneScale / 2;
                return significant;
            });
            if (!significant) {
                for (auto const& [cell, count] : contribution) discarded[cell] += count;
                continue;
            }
        }
        useful[index] = 1;
        visitCenters(forms[index], [&](std::size_t cell) { centers[cell] = color; });
        anySample(forms[index], width, height, [&](std::size_t sample) {
            painted[sample] = color;
            return false;
        });
    }

    std::vector<std::uint8_t> covered(samples, 0);
    for (std::size_t index = entries.size(); index-- > 0;) {
        if (!useful[index]) continue;
        auto const& object = *entries[index].object;
        if (!anySample(forms[index], width, height, [&](std::size_t sample) {
                return covered[sample] == 0;
            })) {
            continue;
        }
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

// Cuadrados juntos del mismo color se fusionan aunque cambien de capa.
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
        // Solo bloques en rejilla: el resto no suma sin mover el dibujo.
        if (std::abs(static_cast<float>(box.maxX - box.minX) - object.width) > 0.001f ||
            std::abs(static_cast<float>(box.maxY - box.minY) - object.height) > 0.001f) {
            continue;
        }
        boxes.push_back(box);
    }
    if (boxes.size() < 2) return;

    // Agrupa por color, lado de fondo y banda.
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
    return object.kind == PrimitiveKind::Block || object.kind == PrimitiveKind::Stroke;
}

bool rectPairShapes(Primitive const& first, Primitive const& second) {
    return rectShape(first) && rectShape(second);
}

// Caja comun de dos rectangulos con su sobrante.
bool unitedRect(
    Primitive const& first,
    Primitive const& second,
    Primitive& result,
    float& extra,
    float& covered
) {
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

// Mismo color nunca se intercala: juntar en capa baja no cambia nada.
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
    // Entre capas solo union exacta: sin mapa que avale el sobrante.
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

// Sobrante gratis si mantiene dibujo; foreign limita encima.
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
                    // Hueco excesivo no es fusion: quita sitio a la ronda siguiente.
                    if (extra > covered * kAbsorbSlack) continue;

                    // Sobrante vale si mantiene el color o lo tapa capa superior.
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

                    // Fusiones de la ronda no pueden tocarse entre si.
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
    // Pixel no perdona centros en vacio; Suave relaja el borde.
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

    // spare: celdas que el rectangulo puede cruzar sin cambiar el dibujo.
    std::vector<std::uint8_t> spare(cells, 0);
    for (int position : paint) spare[static_cast<std::size_t>(position)] = 1;
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            spare[position] |= blocked[position];
        }
    }

    // Escalera une peldanos con tira girada; sin pareja va a rectangulo.
    auto diagonalStrokes = [&](std::vector<int> const& group) {
        std::vector<Primitive> strokes;
        std::vector<int> unpaired;
        std::vector<std::uint8_t> remaining(cells, 0);
        for (int position : group) remaining[static_cast<std::size_t>(position)] = 1;
        auto nearMask = nearCells(group, width, height);
        // Suave relaja el vacio; Pixel no perdona.
        if (gridExact) maskVoid(nearMask, empty);
        for (int first : group) {
            if (!remaining[static_cast<std::size_t>(first)]) continue;
            int const firstX = first % width;
            int const firstY = first / width;
            Primitive best;
            int bestCount = 1;
            float bestLength = 0.f;
            int const minX = std::max(0, firstX - kRepairReach);
            int const minY = std::max(0, firstY - kRepairReach);
            int const maxX = std::min(width - 1, firstX + kRepairReach);
            int const maxY = std::min(height - 1, firstY + kRepairReach);
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    int const second = y * width + x;
                    if (second == first || !target[static_cast<std::size_t>(second)]) continue;
                    float const length = std::hypot(
                        static_cast<float>(x - firstX), static_cast<float>(y - firstY));
                    if (length > kRepairReach) continue;
                    Primitive candidate = repairStroke(first, second, width, color, layer);
                    // Diagonal suelta: disco en vez de tira para no dejar agujas.
                    if (firstX != x && firstY != y && length <= 1.5f) {
                        // 1.8 cubre ambos centros a escala 8 sin salirse de la orla.
                        float const diameter = 1.8f;
                        Primitive round{
                            (static_cast<float>(firstX + x) + 1.f) * 0.5f,
                            (static_cast<float>(firstY + y) + 1.f) * 0.5f,
                            diameter, diameter, 0.f,
                            static_cast<std::uint16_t>(color),
                            PrimitiveKind::Circle,
                            static_cast<std::int16_t>(layer)
                        };
                        if (!coversBlocked(round, width, height, blocked) &&
                            fitsPaintNear(
                                round, permitted, nearMask, width, height, gridExact)) {
                            candidate = round;
                        }
                    }
                    if (!fitsPaintNear(
                            candidate, permitted, nearMask, width, height, gridExact)) {
                        continue;
                    }
                    int const count = coveredRepairs(candidate, remaining, width, height);
                    if (count > bestCount || (count == bestCount && length < bestLength)) {
                        best = candidate;
                        bestCount = count;
                        bestLength = length;
                    }
                }
            }
            if (bestLength <= 0.f) {
                remaining[static_cast<std::size_t>(first)] = 0;
                unpaired.push_back(first);
                continue;
            }
            strokes.push_back(best);
            consumeRepairs(best, remaining, width, height);
        }
        appendBlocks(strokes, unpaired, width, height, color, layer, spare);
        return strokes;
    };

    std::vector<int> leftover;
    for (auto const& component : connectedComponents(positions, width, height)) {
        // Mancha rectangular: va al empaquetado final.
        auto rectangle = packBlocks(component, width, height, color, spare);
        // Girada de una pieza compensa si recta necesitaria varias.
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
    auto strokes = diagonalStrokes(leftover);
    if (!strokes.empty() && strokes.size() < plain.size()) {
        output.insert(output.end(), strokes.begin(), strokes.end());
    } else {
        output.insert(output.end(), plain.begin(), plain.end());
    }
}

// Girada diminuta: se prueba disco, luego bloque recto, luego celda.
std::vector<int> normalizePaintSpikes(
    std::vector<Primitive>& objects,
    std::vector<int> const& target,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& permitted,
    bool gridExact = true
) {
    // Suave admite lado 4 para cerrar curvas en disco; Pixel queda en 3.
    float const maxSpikeSide = gridExact ? 3.f : 4.f;
    constexpr float kStraightAngle = 7.f;
    std::vector<int> repairs;
    std::vector<Primitive> normalized;
    normalized.reserve(objects.size());
    // Orla Suave: solo la usan las giradas, se calcula una vez.
    std::vector<std::uint8_t> smoothNear;
    if (!gridExact) smoothNear = nearCells(target, width, height);
    for (auto const& object : objects) {
        float folded = std::fmod(std::abs(object.rotation), 90.f);
        folded = std::min(folded, 90.f - folded);
        bool const isRotatedStroke = object.kind == PrimitiveKind::Stroke;
        bool const isRotatedTriangle =
            object.kind == PrimitiveKind::Triangle ||
            object.kind == PrimitiveKind::WideTriangle;
        if ((!isRotatedStroke && !isRotatedTriangle) ||
            folded <= kStraightAngle || object.width > maxSpikeSide ||
            object.height > maxSpikeSide) {
            normalized.push_back(object);
            continue;
        }
        // Suave conserva borde desde 2 celdas.
        constexpr float kSmoothKeepSide = 2.f;
        if (!gridExact &&
            std::max(object.width, object.height) >= kSmoothKeepSide &&
            fitsPaintOutline(object, permitted, smoothNear, width, height,
                             gridExact)) {
            normalized.push_back(object);
            continue;
        }

        bool replaced = false;
        float const diameter = std::min(object.width, object.height);
        if (diameter >= 0.75f) {
            Primitive circle{
                object.x, object.y, diameter, diameter, 0.f,
                static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
                object.layer};
            if (!coversBlocked(circle, width, height, blocked) &&
                fitsPaintBoundary(circle, permitted, width, height, gridExact)) {
                normalized.push_back(circle);
                replaced = true;
            }
        }
        if (!replaced) {
            // Varios tamanos: la astilla al borde no fuerza un cuadrado fuera.
            for (float scale : {1.f, 0.9f, 0.75f, 0.6f}) {
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
        }
        if (replaced) continue;

        // El centro se reempaqueta recto en la llamada siguiente.
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

// Solo la punta libre lleva disco; la union escondida no.
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
    if (objects.empty() || target.empty()) return;
    std::vector<std::uint8_t> targetMask(
        static_cast<std::size_t>(width) * height, 0);
    for (int position : target) {
        if (position >= 0 && position < width * height) {
            targetMask[static_cast<std::size_t>(position)] = 1;
        }
    }
    auto nearMask = nearCells(target, width, height);
    // Pintura-Suave relaja el vacio para el borde continuo; en Pixel no perdona.
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
        if (object.kind != PrimitiveKind::Stroke || object.color != color) {
            rounded.push_back(object);
            continue;
        }
        float folded = std::fmod(std::abs(object.rotation), 90.f);
        folded = std::min(folded, 90.f - folded);
        float const thickness = std::min(object.width, object.height);
        float const length = std::max(object.width, object.height);
        // Corta o cuadrada no lleva disco; banda curva con extra si.
        if (folded <= 10.f || thickness < kRoundCapMinThickness ||
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
            // Celda del color delante: es continuacion y queda cubierta.
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
            float const cornerGap = std::max(
                targetGap({edge.x + normal.x * thickness * 0.5f,
                           edge.y + normal.y * thickness * 0.5f}),
                targetGap({edge.x - normal.x * thickness * 0.5f,
                           edge.y - normal.y * thickness * 0.5f}));
            // Solo remata el pico separado; al borde pegado le va el rectangulo.
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
        // El suavizado nunca quita un centro que la tira cubria.
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

// Carrera diagonal de bloques 1x1: se vuelve tira girada con discos.
void smoothDiagonalBlockRuns(
    std::vector<Primitive>& objects,
    std::vector<int> const& target,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& permitted,
    std::vector<std::uint8_t> const& empty,
    bool gridExact
) {
    // Solo Pintura-Suave: Pixel no cambia cobertura por tira subpixel.
    if (gridExact || target.size() < 3 || objects.size() < 3 || width <= 0 || height <= 0) {
        return;
    }
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    if (permitted.size() != cells) return;

    std::vector<std::uint8_t> targetMask(cells, 0);
    for (int position : target) {
        if (position >= 0 && static_cast<std::size_t>(position) < cells) {
            targetMask[static_cast<std::size_t>(position)] = 1;
        }
    }
    auto nearMask = nearCells(target, width, height);
    if (gridExact) maskVoid(nearMask, empty);

    // Solo piezas pequenas alineadas; la grande es relleno real.
    std::vector<int> owner(cells, -1);
    std::vector<std::vector<int>> owned(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        auto const& object = objects[index];
        if ((object.kind != PrimitiveKind::Block && object.kind != PrimitiveKind::Circle) ||
            object.color != color ||
            std::abs(object.rotation) > 0.01f || object.width > 1.35f ||
            object.height > 1.35f || object.width < 0.65f || object.height < 0.65f) {
            continue;
        }
        auto const placed = xformOf(object);
        auto const box = xformBox(placed, width, height);
        for (int y = box[1]; y <= box[3]; ++y) {
            for (int x = box[0]; x <= box[2]; ++x) {
                auto const cell = static_cast<std::size_t>(y) * width + x;
                if (!targetMask[cell] || !placed.contains(x + 0.5f, y + 0.5f)) continue;
                if (owner[cell] >= 0) {
                    // Solape de dos reparos: se deja intacto, no es carrera.
                    owner[cell] = -2;
                    continue;
                }
                owner[cell] = static_cast<int>(index);
                owned[index].push_back(static_cast<int>(cell));
            }
        }
    }

    struct Candidate {
        std::vector<int> cells;
        std::vector<int> owners;
        std::vector<Primitive> shapes;
    };
    std::vector<Candidate> candidates;
    constexpr int kDirections[2][2]{{1, 1}, {1, -1}};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int const start = y * width + x;
            if (owner[static_cast<std::size_t>(start)] < 0) continue;
            for (auto const& direction : kDirections) {
                int const previousX = x - direction[0];
                int const previousY = y - direction[1];
                if (previousX >= 0 && previousY >= 0 && previousX < width &&
                    previousY < height &&
                    owner[static_cast<std::size_t>(previousY) * width + previousX] >= 0) {
                    continue;
                }

                std::vector<int> run;
                int currentX = x;
                int currentY = y;
                while (currentX >= 0 && currentY >= 0 && currentX < width &&
                       currentY < height) {
                    int const position = currentY * width + currentX;
                    if (owner[static_cast<std::size_t>(position)] < 0) break;
                    run.push_back(position);
                    currentX += direction[0];
                    currentY += direction[1];
                }
                if (run.size() < 3) continue;

                std::vector<int> uniqueOwners;
                for (int position : run) {
                    int const index = owner[static_cast<std::size_t>(position)];
                    if (index < 0 || std::find(uniqueOwners.begin(), uniqueOwners.end(), index) !=
                            uniqueOwners.end()) {
                        continue;
                    }
                    uniqueOwners.push_back(index);
                }
                if (uniqueOwners.size() < 3) continue;

                // Un bloque candidato no puede esconder otras celdas de su objeto.
                bool ownsOnlyRun = true;
                for (int index : uniqueOwners) {
                    for (int position : owned[static_cast<std::size_t>(index)]) {
                        if (std::find(run.begin(), run.end(), position) == run.end()) {
                            ownsOnlyRun = false;
                            break;
                        }
                    }
                    if (!ownsOnlyRun) break;
                }
                if (!ownsOnlyRun) continue;

                Point const first{
                    static_cast<float>(run.front() % width) + 0.5f,
                    static_cast<float>(run.front() / width) + 0.5f};
                Point const last{
                    static_cast<float>(run.back() % width) + 0.5f,
                    static_cast<float>(run.back() / width) + 0.5f};
                float const dx = last.x - first.x;
                float const dy = last.y - first.y;
                float const length = std::hypot(dx, dy);
                if (length < 2.7f) continue;

                float thickness = 1.f;
                for (int index : uniqueOwners) {
                    thickness = std::max(
                        thickness,
                        std::min(objects[static_cast<std::size_t>(index)].width,
                                 objects[static_cast<std::size_t>(index)].height));
                }
                thickness = std::clamp(thickness, 0.9f, 1.25f);
                Point const center{(first.x + last.x) * 0.5f, (first.y + last.y) * 0.5f};
                float const angle = std::atan2(dy, dx) * 180.f / kPi;
                Primitive stroke{
                    center.x, center.y, length + 0.08f, thickness, angle,
                    static_cast<std::uint16_t>(color), PrimitiveKind::Stroke,
                    objects[static_cast<std::size_t>(uniqueOwners.front())].layer};

                bool fits = fitsPaintOutline(
                    stroke, permitted, nearMask, width, height, gridExact);
                // Suave: carrera corta roza la orla; Near la cose sin escalera.
                if (!fits && !gridExact) {
                    fits = fitsPaintNear(
                        stroke, permitted, nearMask, width, height, gridExact);
                }
                if (!fits) {
                    continue;
                }

                std::vector<Primitive> shapes{stroke};
                std::vector<Primitive> caps;
                bool capsFit = true;
                for (std::size_t end = 0; end < 2; ++end) {
                    int const endpoint = run[end == 0 ? 0 : run.size() - 1];
                    int const endpointX = endpoint % width;
                    int const endpointY = endpoint / width;
                    int const stepX = end == 0 ? -direction[0] : direction[0];
                    int const stepY = end == 0 ? -direction[1] : direction[1];
                    int const nextX = endpointX + stepX;
                    int const nextY = endpointY + stepY;
                    bool endpointOpen = nextX < 0 || nextY < 0 || nextX >= width || nextY >= height ||
                        !targetMask[static_cast<std::size_t>(nextY) * width + nextX];
                    for (int neighborY = endpointY - 1; neighborY <= endpointY + 1 && endpointOpen;
                         ++neighborY) {
                        for (int neighborX = endpointX - 1; neighborX <= endpointX + 1;
                             ++neighborX) {
                            if (neighborX < 0 || neighborY < 0 || neighborX >= width ||
                                neighborY >= height || (neighborX == endpointX && neighborY == endpointY)) {
                                continue;
                            }
                            int const neighbor = neighborY * width + neighborX;
                            if (!targetMask[static_cast<std::size_t>(neighbor)] ||
                                std::find(run.begin(), run.end(), neighbor) != run.end()) {
                                continue;
                            }
                            endpointOpen = false;
                            break;
                        }
                    }
                    if (!endpointOpen) continue;
                    Point const point = end == 0 ? first : last;
                    Primitive cap{
                        point.x, point.y, thickness, thickness, 0.f,
                        static_cast<std::uint16_t>(color), PrimitiveKind::Circle,
                        stroke.layer};
                    if (coversBlocked(cap, width, height, blocked) ||
                        !fitsPaintNear(
                            cap, permitted, nearMask, width, height, gridExact)) {
                        capsFit = false;
                        break;
                    }
                    caps.push_back(cap);
                }
                if (capsFit) shapes.insert(shapes.end(), caps.begin(), caps.end());

                auto const forms = xformsOf(shapes);
                bool preserves = true;
                for (int position : run) {
                    float const px = static_cast<float>(position % width) + 0.5f;
                    float const py = static_cast<float>(position / width) + 0.5f;
                    if (std::none_of(forms.begin(), forms.end(), [&](ShapeXform const& form) {
                            return form.contains(px, py);
                        })) {
                        preserves = false;
                        break;
                    }
                }
                if (!preserves) continue;
                candidates.push_back({std::move(run), std::move(uniqueOwners), std::move(shapes)});
            }
        }
    }

    // Escalera alterna: una tira entre extremos si no se desvia.
    std::vector<std::uint8_t> componentSeen(cells, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int const start = y * width + x;
            if (owner[static_cast<std::size_t>(start)] < 0 ||
                componentSeen[static_cast<std::size_t>(start)] != 0) {
                continue;
            }
            std::vector<int> component{start};
            componentSeen[static_cast<std::size_t>(start)] = 1;
            for (std::size_t cursor = 0; cursor < component.size(); ++cursor) {
                int const position = component[cursor];
                int const positionX = position % width;
                int const positionY = position / width;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int const neighborX = positionX + dx;
                        int const neighborY = positionY + dy;
                        if (neighborX < 0 || neighborY < 0 || neighborX >= width ||
                            neighborY >= height) {
                            continue;
                        }
                        int const neighbor = neighborY * width + neighborX;
                        if (owner[static_cast<std::size_t>(neighbor)] < 0 ||
                            componentSeen[static_cast<std::size_t>(neighbor)] != 0) {
                            continue;
                        }
                        componentSeen[static_cast<std::size_t>(neighbor)] = 1;
                        component.push_back(neighbor);
                    }
                }
            }
            // Tope 256: el ajuste es cuadratico y la mancha grande no es escalera.
            if (component.size() < 4 || component.size() > 256) continue;

            std::vector<int> uniqueOwners;
            for (int position : component) {
                int const index = owner[static_cast<std::size_t>(position)];
                if (index >= 0 && std::find(uniqueOwners.begin(), uniqueOwners.end(), index) ==
                        uniqueOwners.end()) {
                    uniqueOwners.push_back(index);
                }
            }
            if (uniqueOwners.size() < 4) continue;

            bool ownsOnlyComponent = true;
            for (int index : uniqueOwners) {
                for (int position : owned[static_cast<std::size_t>(index)]) {
                    if (std::find(component.begin(), component.end(), position) == component.end()) {
                        ownsOnlyComponent = false;
                        break;
                    }
                }
                if (!ownsOnlyComponent) break;
            }
            if (!ownsOnlyComponent) continue;

            float farthestDistance = 0.f;
            int firstIndex = 0;
            int lastIndex = 0;
            for (std::size_t first = 0; first < component.size(); ++first) {
                for (std::size_t last = first + 1; last < component.size(); ++last) {
                    float const dx = static_cast<float>(component[last] % width -
                        component[first] % width);
                    float const dy = static_cast<float>(component[last] / width -
                        component[first] / width);
                    float const distance = std::hypot(dx, dy);
                    if (distance > farthestDistance) {
                        farthestDistance = distance;
                        firstIndex = static_cast<int>(first);
                        lastIndex = static_cast<int>(last);
                    }
                }
            }
            if (farthestDistance < 3.f) continue;

            Point const first{
                static_cast<float>(component[static_cast<std::size_t>(firstIndex)] % width) + 0.5f,
                static_cast<float>(component[static_cast<std::size_t>(firstIndex)] / width) + 0.5f};
            Point const last{
                static_cast<float>(component[static_cast<std::size_t>(lastIndex)] % width) + 0.5f,
                static_cast<float>(component[static_cast<std::size_t>(lastIndex)] / width) + 0.5f};
            float const dx = last.x - first.x;
            float const dy = last.y - first.y;
            float const length = std::hypot(dx, dy);
            float folded = std::fmod(std::abs(std::atan2(dy, dx) * 180.f / kPi), 90.f);
            folded = std::min(folded, 90.f - folded);
            if (folded < 8.f || folded > 82.f) continue;

            float maximumOffset = 0.f;
            for (int position : component) {
                float const px = static_cast<float>(position % width) + 0.5f - first.x;
                float const py = static_cast<float>(position / width) + 0.5f - first.y;
                maximumOffset = std::max(
                    maximumOffset, std::abs(px * dy - py * dx) / length);
            }
            if (maximumOffset > 0.9f || length / std::max(maximumOffset * 2.f, 1.f) < 2.4f) {
                continue;
            }
            float const thickness = std::clamp(maximumOffset * 2.f + 0.25f, 1.05f, 1.8f);
            Point const center{(first.x + last.x) * 0.5f, (first.y + last.y) * 0.5f};
            Primitive stroke{
                center.x, center.y, length + 0.08f, thickness,
                std::atan2(dy, dx) * 180.f / kPi,
                static_cast<std::uint16_t>(color), PrimitiveKind::Stroke,
                objects[static_cast<std::size_t>(uniqueOwners.front())].layer};
            // Puerta de borde completo: el extra de grosor no invade al vecino.
            bool fits = fitsPaintBoundary(stroke, permitted, width, height, gridExact);
            if (!fits && !gridExact) {
                fits = fitsPaintNear(
                    stroke, permitted, nearMask, width, height, gridExact);
            }
            if (!fits) {
                continue;
            }
            auto const form = xformOf(stroke);
            bool preserves = true;
            for (int position : component) {
                if (!form.contains(
                        static_cast<float>(position % width) + 0.5f,
                        static_cast<float>(position / width) + 0.5f)) {
                    preserves = false;
                    break;
                }
            }
            if (!preserves) continue;
            candidates.push_back({std::move(component), std::move(uniqueOwners), {stroke}});
        }
    }
    if (candidates.empty()) return;

    std::sort(candidates.begin(), candidates.end(), [](Candidate const& left, Candidate const& right) {
        return left.cells.size() > right.cells.size();
    });
    std::vector<std::uint8_t> consumed(objects.size(), 0);
    std::vector<Primitive> additions;
    std::vector<std::uint8_t> remove(objects.size(), 0);
    for (auto const& candidate : candidates) {
        if (std::any_of(candidate.owners.begin(), candidate.owners.end(), [&](int index) {
                return consumed[static_cast<std::size_t>(index)] != 0;
            })) {
            continue;
        }
        for (int index : candidate.owners) {
            consumed[static_cast<std::size_t>(index)] = 1;
            remove[static_cast<std::size_t>(index)] = 1;
        }
        additions.insert(additions.end(), candidate.shapes.begin(), candidate.shapes.end());
    }
    if (additions.empty()) return;
    std::vector<Primitive> smoothed;
    smoothed.reserve(objects.size() + additions.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        if (!remove[index]) smoothed.push_back(objects[index]);
    }
    smoothed.insert(smoothed.end(), additions.begin(), additions.end());
    objects = std::move(smoothed);
}

// Sobra el objeto que no cambia ningun pixel al borrarlo.
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

        std::vector<std::uint8_t> needed(ordered.size(), 0);
        for (std::size_t sample = 0; sample < samples; ++sample) {
            auto const owner = top[sample];
            if (owner < 0) continue;
            auto const under = below[sample];
            if (under >= 0 &&
                ordered[static_cast<std::size_t>(under)]->color ==
                    ordered[static_cast<std::size_t>(owner)]->color) {
                continue;
            }
            needed[static_cast<std::size_t>(owner)] = 1;
        }

        std::vector<std::uint8_t> keep(objects.size(), 0);
        for (std::size_t slot = 0; slot < ordered.size(); ++slot) {
            if (!needed[slot]) {
                dropped = true;
                continue;
            }
            keep[static_cast<std::size_t>(ordered[slot] - objects.data())] = 1;
        }
        if (dropped) compactKept(objects, keep);
    }
}

} // namespace

std::vector<Primitive> paintSeamRepairs(
    std::vector<Primitive> const& objects,
    std::vector<std::int32_t> const& cells,
    std::vector<int> const& ranks,
    int width,
    int height,
    bool gridExact
) {
    (void)gridExact;
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
    // Un solo color: el borde curvo es de la figura; costura solo entre colores.
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
            // Fondo por color bajo el dibujo: evita empates entre colores.
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
            // Parche redondo: el bloque 1x1 lo parte la marca de agua.
            Primitive patch{
                position % width + 0.5f,
                position / width + 0.5f,
                1.f,
                1.f,
                0.f,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Circle,
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

// Cose remates del mismo color y giro en rectangulo sin pintar de mas.
void mergePaintSolids(std::vector<Primitive>& objects, bool gridExact) {
    (void)gridExact;
    if (objects.size() < 2) return;
    mergePaintBlocks(objects);
    mergePaintRects(objects, true);
}

void prunePaintObjects(std::vector<Primitive>& objects, int width, int height, bool gridExact) {
    (void)gridExact;
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

    // El objeto se queda si hace falta en algun frame.
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
        // Cada pista se fusiona aparte: distinto frame no es lo mismo.
        mergePaintBlocks(tracks[track].objects);
        mergePaintRects(tracks[track].objects);
        absorbPaintRects(tracks[track].objects, width, height);
        dropRedundantObjects(tracks[track].objects, width, height);
    }

    // Pistas como terreno prohibido: el fijo crecido no las pisa.
    std::vector<Primitive const*> animated;
    for (auto const& track : tracks) {
        for (auto const& object : track.objects) animated.push_back(&object);
    }
    absorbPaintRects(staticObjects, width, height, animated);
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

    // Encerrado va encima; celda que cambia de color no restringe.
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
                        // -1 es transparencia/exterior; -2 es mezcla temporal.
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

    // Topologico sobre heuristico; el ciclo se corta por prioridad.
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
    int const base = rank * kPaintSublayers;
    // spare: celdas que el rectangulo cruza sin cambiar el dibujo.
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
    // Pixel no perdona el vacio; Suave lo relaja para el borde.
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
            appendRepairs(
                output,
                selectCells(
                    region, width, coverageMask(region, output, false), {}, true, 0.f),
                width, height, color, base + 2, blocked, positions, empty, gridExact);
            roundExposedStrokeEnds(
                output, positions, width, height, color, blocked, permitted, empty,
                gridExact);
            auto spikeRepairs = normalizePaintSpikes(
                output, positions, width, height, color, blocked, permitted,
                gridExact);
            if (!spikeRepairs.empty()) {
                appendRepairs(
                    output, spikeRepairs, width, height, color, base + 2,
                    blocked, spikeRepairs, empty, gridExact);
            }
            smoothDiagonalBlockRuns(
                output, positions, width, height, color, blocked, permitted, empty,
                gridExact);
            return output;
        }
    }
    std::vector<std::vector<int>> pieces;
    for (auto const& whole : connectedComponents(positions, width, height)) {
        // Silueta antes que grosor: dos discos no hacen una elipse.
        if (whole.size() >= 8 && whole.size() != positions.size()) {
            auto const region = buildRegion(whole, width);
            std::vector<Primitive> fitted;
            if (appendCircle(fitted, region, color, base,
                             width, height, blocked) ||
                appendTriangle(fitted, whole, width, height, color, base, blocked) ||
                appendCapsule(fitted, whole, width, height, color, base + 1,
                              blocked, empty)) {
                appendRepairs(fitted,
                    selectCells(region, width, coverageMask(region, fitted, false),
                                {}, true, 0.f),
                    width, height, color, base + 2, blocked, positions, empty, gridExact);
                roundExposedStrokeEnds(
                    fitted, whole, width, height, color, blocked, permitted, empty,
                    gridExact);
                auto spikeRepairs = normalizePaintSpikes(
                    fitted, whole, width, height, color, blocked, permitted,
                    gridExact);
                if (!spikeRepairs.empty()) {
                    appendRepairs(
                        fitted, spikeRepairs, width, height, color, base + 2,
                        blocked, spikeRepairs, empty, gridExact);
                }
                smoothDiagonalBlockRuns(
                    fitted, whole, width, height, color, blocked, permitted, empty,
                    gridExact);
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
        // Mismo color: una pasada junta rectangulos grandes.
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
        if (!fitted) {
            // Cadena solo si es trazo; si es compacta va por contorno.
            chained = radius <= kThinRadius &&
                appendChain(
                    shapes, region, component, width, height, radius, color, base + 1,
                    blocked, permitted, empty, gridExact);
            if (!chained) {
                float const band = std::clamp(radius * 1.4f, 1.4f, kBandWidth);
                std::vector<Contour> refined;
                for (auto const& contour : traceContours(region)) {
                    if (contour.points.size() < 3) continue;
                    // Tira fina: recortar poco o se ve pico.
                    float const local =
                        contourThickness(region, contour, band, gridExact);
                    refined.push_back(refineContour(
                        contour, local <= 2.5f ? 1 : 2,
                        std::clamp(local * 0.3f, 0.3f, kSmoothTolerance)));
                }
                std::vector<Primitive> outline;
                for (auto const& contour : refined) {
                    appendBand(
                        outline, region, contour, band, color, base + 1,
                        width, height, blocked, permitted, gridExact);
                }
                // Orla solo en giradas; en rectas solo suma derrame.
                auto nearMask = nearCells(component, width, height);
                // Pintura-Suave relaja el vacio para el borde continuo; en Pixel no perdona.
                if (gridExact) maskVoid(nearMask, empty);
                outline.erase(std::remove_if(outline.begin(), outline.end(),
                    [&](Primitive const& stroke) {
                        return !fitsPaintOutline(
                            stroke, permitted, nearMask, width, height, gridExact);
                    }), outline.end());
                inside = insideContours(region, refined);
                // El relleno salido muerde al vecino: fuera solo si tapa otro.
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
        }

        collectPlain(
            coverageMask(region, shapes, false), inside, true, chained ? 1.1f : 1.6f);
        // Relleno bajo celdas a medio tapar; con cadena sobra.
        if (!chained) {
            collectPlain(
                coverageMask(region, shapes, true, gridExact), {}, true, 1.01f);
        }
        appendBlocks(shapes, plain, width, height, color, base, spare);
        // Hueco aqui muestra al vecino: sin mascara para no dejar agujeros.
        auto missing = selectCells(
            region, width, coverageMask(region, shapes, false), {}, true, 0.f);
        repairs.insert(repairs.end(), missing.begin(), missing.end());
        output.insert(output.end(), shapes.begin(), shapes.end());
    }

    appendRepairs(
        output, repairs, width, height, color, base + 2, blocked, positions, empty,
        gridExact);

    // Ultima barrera: los reparos tambien pueden dejar astillas.
    roundExposedStrokeEnds(
        output, positions, width, height, color, blocked, permitted, empty, gridExact);
    auto spikeRepairs = normalizePaintSpikes(
        output, positions, width, height, color, blocked, permitted, gridExact);
    if (!spikeRepairs.empty()) {
        appendRepairs(
            output, spikeRepairs, width, height, color, base + 2,
            blocked, spikeRepairs, empty, gridExact);
        // Segunda pasada barata: idempotente para curvas pequenas.
        auto leftover = normalizePaintSpikes(
            output, spikeRepairs, width, height, color, blocked, permitted,
            gridExact);
        if (!leftover.empty()) {
            appendBlocks(output, leftover, width, height, color, base + 2);
        }
    }

    smoothDiagonalBlockRuns(
        output, positions, width, height, color, blocked, permitted, empty, gridExact);

    std::stable_sort(output.begin(), output.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
    return output;
}

} // namespace paimon::gifimport
