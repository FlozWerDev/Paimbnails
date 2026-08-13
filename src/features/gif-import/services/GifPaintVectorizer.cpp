#include "GifPaintVectorizer.hpp"

#include "GifArtVectorizer.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace paimon::gifimport {

namespace {

constexpr float kBandWidth = 3.f;
constexpr float kBandMiter = 0.6f;
constexpr float kOvershoot = 0.72f;
constexpr float kFreeOvershoot = 0.2f;
constexpr float kSmoothTolerance = 0.9f;
constexpr float kThinRadius = 4.25f;
// Medio lado del cuadrado que separa una zona maciza de un trazo, en celdas: por
// encima de nueve celdas de ancho la mancha ya no es una linea del dibujo.
constexpr int kThickSpan = 5;
// Cuanto puede sobrar alrededor de una mancha metida en su caja girada. Una tira
// en diagonal llena su caja y cabe; una mancha en ele o en ese deja media caja
// vacia, y esa media caja se pinta encima de lo que hubiera debajo.
constexpr float kPatchSlack = 1.7f;
// Lo que cuesta asomar una celda sobre lo que otra capa tapa despues, comparado con
// asomar sobre el dibujo. Barato para que el truco de las capas siga valiendo, pero
// no gratis.
constexpr float kCoveredSpill = 0.35f;
// Cuantas veces su grosor tiene que medir un trazo de largo para que valga la pena
// trazarlo como tal.
constexpr float kChainSlenderness = 3.f;
constexpr float kRepairDiameter = 1.f;
constexpr float kRoundCapDiameter = 1.8f;
constexpr int kRepairReach = 3;
constexpr int kPadding = 2;
// Con cuatro muestras por lado se ve cualquier asomo de mas de un cuarto de celda,
// que es justo lo que se nota en pantalla.
constexpr int kFitSamples = 4;

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

// Distancia de tablero de ajedrez a la celda vacia mas cercana, contando el
// exterior como vacio. Sirve para medir el grosor con un cuadrado en vez de con un
// circulo, que es lo que hace falta para separar una zona maciza de un trazo: al
// cuadrado el circulo le corta las cuatro esquinas y las manda al trazo.
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

// Una misma mancha suele llevar pegados el trazo del dibujo y una zona maciza del
// mismo color: la linea del contorno nace del mechon relleno. Medidos juntos manda
// el grosor del mechon, y el trazo sale convertido en una banda gorda que se come
// el color de al lado. Se separa lo que cabe dentro de un cuadrado grande de lo que
// no, y cada parte va por su camino: la maciza a rectangulos, el trazo a tiras. Un
// rectangulo entero cabe entero en el cuadrado y no se parte.
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
    if (object.kind == PrimitiveKind::Triangle ||
        object.kind == PrimitiveKind::WideTriangle) {
        float const u = localX / object.width + 0.5f;
        float const v = localY / object.height + 0.5f;
        return u >= 0.f && v >= 0.f && u <= 1.f && v <= 1.f && u + v <= 1.f;
    }
    return std::abs(localX) <= object.width * 0.5f &&
        std::abs(localY) <= object.height * 0.5f;
}

std::array<int, 4> shapeBox(Primitive const& shape, int width, int height) {
    float const angle = shape.rotation * kPi / 180.f;
    float const extentX = std::abs(std::cos(angle)) * shape.width * 0.5f +
        std::abs(std::sin(angle)) * shape.height * 0.5f;
    float const extentY = std::abs(std::sin(angle)) * shape.width * 0.5f +
        std::abs(std::cos(angle)) * shape.height * 0.5f;
    return {
        std::max(0, static_cast<int>(std::floor(shape.x - extentX))),
        std::max(0, static_cast<int>(std::floor(shape.y - extentY))),
        std::min(width - 1, static_cast<int>(std::ceil(shape.x + extentX))),
        std::min(height - 1, static_cast<int>(std::ceil(shape.y + extentY)))
    };
}

// Una figura puede asomar de sus celdas solo hacia donde no se nota: celdas del
// mismo color, celdas que otro color tapa despues, o hueco que ningun frame
// pinta. Asomando sobre el color que queda debajo es cuando se ve el pico, y
// medir por el centro de la celda no lo detecta porque el pico entra menos de
// media celda.
bool shapeStaysInside(
    Primitive const& shape,
    std::vector<std::uint8_t> const& permitted,
    int width,
    int height
) {
    auto const box = shapeBox(shape, width, height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            if (permitted[static_cast<std::size_t>(y) * width + x]) continue;
            for (int sampleY = 0; sampleY < kFitSamples; ++sampleY) {
                for (int sampleX = 0; sampleX < kFitSamples; ++sampleX) {
                    if (insideShape(
                            shape,
                            static_cast<float>(x) + (sampleX + 0.5f) / kFitSamples,
                            static_cast<float>(y) + (sampleY + 0.5f) / kFitSamples)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

// Un objeto redondo solo puede ir donde nada se pinte encima: GD lo dibuja en
// otra hoja de sprites y ninguna capa Z lo devuelve detras de los cuadrados.
bool coversBlocked(
    Primitive const& shape,
    int sourceWidth,
    int sourceHeight,
    std::vector<std::uint8_t> const& blocked
) {
    if (blocked.size() != static_cast<std::size_t>(sourceWidth) * sourceHeight) return false;
    auto const box = shapeBox(shape, sourceWidth, sourceHeight);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            if (!blocked[static_cast<std::size_t>(y) * sourceWidth + x]) continue;
            if (insideShape(shape, x + 0.5f, y + 0.5f)) return true;
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

// Lo justo para que los bordes de fuera de dos tiras seguidas se toquen. Pasarse
// deja un pincho en cada vertice del contorno; el tope evita la aguja infinita
// cuando el trazo se dobla sobre si mismo. En el contorno de una silueta el tope
// va mas corto que en un trazo suelto: ahi el pincho sale a la vista sobre el
// color de al lado, y la muesca que deja la esquina la rellena despues un cuadrado
// justo, que no sobresale.
float miterExtension(float dot, float thickness, float limit) {
    limit = std::min(limit, thickness * 0.5f);
    if (dot <= 0.f) return limit;
    return std::min(limit, thickness * 0.5f * std::tan(std::acos(dot) * 0.5f));
}

// El remate redondo solo cabe si es lo bastante grande para que se note y si no
// tiene que quedar debajo de otro color. Cuando no cabe no se pone nada: el
// llamante alarga el trazo, porque un cuadrado girado en la punta se ve como un
// pico y encima gasta un objeto.
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

// Lo que mide la mancha justo debajo de un punto del contorno: se camina hacia
// dentro hasta salir por el otro lado. Una silueta de dos celdas de ancho da dos
// aunque la misma mancha lleve pegado un bulto de veinte, que es lo que pasa
// cuando el trazo del dibujo y una zona rellena son del mismo color.
float inwardThickness(
    Region const& region,
    float x,
    float y,
    float inwardX,
    float inwardY,
    float limit
) {
    constexpr float kStep = 0.25f;
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

// Lo que mide la mancha a lo largo de un contorno, por la mediana para que un
// bulto suelto no mande. Marca hasta donde se puede recortar el contorno al
// simplificarlo: en una linea de dos celdas, cortar una esquina por casi una
// celda es lo que dejaba los picos y las mordidas.
float contourThickness(Region const& region, Contour const& contour, float limit) {
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
            inwardThickness(region, midX, midY, inwardX, inwardY, limit));
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
    std::vector<std::uint8_t> const& blocked
) {
    auto const& loop = contour.points;
    if (loop.size() < 2) return;
    std::size_t const segments = contour.closed ? loop.size() : loop.size() - 1;
    auto const measured = measure(loop, segments);

    // Contra el color de al lado conviene pasarse, que la costura se tapa; contra
    // el vacio solo hace falta lo justo para compensar lo que recorta el contorno
    // simplificado, o queda un halo alrededor de la silueta.
    bool const hasBlocked =
        blocked.size() == static_cast<std::size_t>(sourceWidth) * sourceHeight;
    auto coveredOutside = [&](float x, float y) {
        int const cellX = static_cast<int>(std::floor(x)) + region.offsetX;
        int const cellY = static_cast<int>(std::floor(y)) + region.offsetY;
        // Fuera del lienzo no hay nada que ensuciar.
        if (cellX < 0 || cellY < 0 || cellX >= sourceWidth || cellY >= sourceHeight) {
            return true;
        }
        if (!hasBlocked) return false;
        return blocked[static_cast<std::size_t>(cellY) * sourceWidth + cellX] != 0;
    };

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
        // El grosor de la tira sale de lo que mide la mancha aqui mismo. Tomarlo
        // de la parte mas gorda de la mancha engordaba el trazo del dibujo hasta
        // comerse el color de al lado, y encima lo pintaba dos veces: una por el
        // contorno de fuera y otra por el de dentro.
        float const thickness = std::max(
            inwardThickness(region, midX, midY, inwardX, inwardY, band), 1.f);

        bool const hasPrevious = contour.closed || i > 0;
        bool const hasNext = contour.closed || i + 1 < segments;
        float const startDot = hasPrevious
            ? directionDot(measured[(i + segments - 1) % segments].direction, segment.direction)
            : 1.f;
        float const endDot = hasNext
            ? directionDot(segment.direction, measured[(i + 1) % segments].direction)
            : 1.f;
        float const startExtent = hasPrevious
            ? miterExtension(startDot, thickness, kBandMiter) : thickness * 0.5f;
        float const endExtent = hasNext
            ? miterExtension(endDot, thickness, kBandMiter) : thickness * 0.5f;

        float const shift = (endExtent - startExtent) * 0.5f;
        // Pasarse nunca puede llegar a medio grosor: en una linea fina eso dejaria
        // la tira entera fuera de la mancha.
        float const overshoot = std::min(
            coveredOutside(midX - inwardX * 0.5f, midY - inwardY * 0.5f)
                ? kOvershoot : kFreeOvershoot,
            thickness * 0.35f);
        float const offset = thickness * 0.5f - overshoot;
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
}

// Devuelve false cuando la mancha no es un trazo y no se dibuja nada: el llamante
// la manda entonces por el camino del contorno.
bool appendChain(
    std::vector<Primitive>& output,
    Region const& region,
    std::vector<int> const& component,
    int sourceWidth,
    int sourceHeight,
    float radius,
    int color,
    int layer,
    std::vector<std::uint8_t> const& blocked
) {
    auto const skeleton = thin(component, sourceWidth);
    auto const paths = skeletonPaths(skeleton);
    // El eje no se puede mover mas de medio grosor del propio trazo: pasado eso la
    // tira se sale por donde no hay mancha y deja al descubierto por donde si la
    // hay, y cada trozo descubierto acaba siendo su propio cuadradito de parche.
    // Con la tolerancia de antes una ondulacion de una celda se aplanaba entera.
    float const tolerance = std::clamp(radius * 0.5f, 0.6f, 1.3f);

    struct Line {
        std::vector<Point> points;
        std::array<bool, 2> joined{};
        std::array<bool, 2> terminal{};
        // Coseno del giro contra la linea con la que empalma; -1 (desconocido)
        // deja el remate cuadrado de medio grosor.
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
        auto reduced = simplify(smoothPath(points, 1), tolerance);
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

    // Un trazo tiene que dar varias anchuras de largo. Si el eje apenas mide mas que
    // el grosor, la mancha es compacta y no tiene eje de verdad: la tira que saldria
    // es una losa girada que no se parece a nada de lo que hay debajo, y encima
    // asoma por las esquinas. Esa mancha va por el contorno.
    if (span < nominal * kChainSlenderness) return false;

    // Las tiras se arman aparte y solo se entregan si el conjunto no se sale de la
    // mancha. El eje sale del adelgazado y puede atajar por donde no hay color; una
    // tira asi cruza el dibujo de lado a lado y borra lo que pilla.
    std::vector<Primitive> strokes;

    // Punta libre: se remata en redondo cuando el circulo cabe, y si no se alarga
    // el trazo medio grosor. Eso es un remate cuadrado que no gasta objeto y que
    // no asoma de lado, al contrario que el cuadrado girado que se usaba antes.
    auto terminalExtension = [&](Point const& point, float thickness) {
        if (appendRoundCap(
                strokes, point, thickness, color, layer,
                sourceWidth, sourceHeight, blocked)) {
            return 0.f;
        }
        return thickness * 0.5f;
    };

    for (auto const& line : lines) {
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
            // El grosor sale de lo que mide la mancha a lo largo del tramo, no
            // solo en el centro; se toma por lo bajo para no salirse donde se
            // estrecha, pero sin hacer caso al peor mordisco del borde.
            float const local = region.distanceAt(
                midX - static_cast<float>(region.offsetX),
                midY - static_cast<float>(region.offsetY));
            float const thicknessScale = radius <= 1.5f ? 1.f : 0.95f;
            float const minimumThickness = radius <= 1.5f ? 0.9f : 0.8f;
            float const thickness = std::clamp(
                local * 2.f, nominal * 0.8f,
                std::max(nominal * thicknessScale, minimumThickness));
            // Aqui el vertice cae en el centro del trazo, asi que el bisel se
            // queda dentro de la mancha y puede ir al tope entero: acortarlo
            // abriria una muesca en mitad de la linea.
            float const miter = thickness * 0.5f;
            float const startExtension = i > 0
                ? miterExtension(
                      directionDot(measured[i - 1].direction, segment.direction),
                      thickness, miter)
                : line.joined[0]
                    ? miterExtension(line.jointDot[0], thickness, miter)
                    : terminalExtension(first, thickness);
            float const endExtension = i + 1 < segments
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
    if (strokes.empty()) return false;
    output.insert(output.end(), strokes.begin(), strokes.end());
    return true;
}

// El circulo es el unico objeto que no es un bloque de color solido: GD lo dibuja
// en otra hoja de sprites, asi que su orden Z no lo puede meter detras de los
// cuadrados. Solo se admite cuando nada se pinta encima, que es cuando flotar
// arriba da igual.
bool appendCircle(
    std::vector<Primitive>& output,
    Region const& region,
    std::size_t area,
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

    bool const hasBlocked =
        blocked.size() == static_cast<std::size_t>(sourceWidth) * sourceHeight;
    int missing = 0;
    int spilled = 0;
    for (int y = 0; y < region.height; ++y) {
        for (int x = 0; x < region.width; ++x) {
            float const sampleX = static_cast<float>(x + region.offsetX) + 0.5f;
            float const sampleY = static_cast<float>(y + region.offsetY) + 0.5f;
            if (!insideShape(circle, sampleX, sampleY)) {
                missing += region.filled(x, y);
                continue;
            }
            int const sourceX = x + region.offsetX;
            int const sourceY = y + region.offsetY;
            if (hasBlocked && sourceX >= 0 && sourceY >= 0 &&
                sourceX < sourceWidth && sourceY < sourceHeight &&
                blocked[static_cast<std::size_t>(sourceY) * sourceWidth + sourceX]) {
                return false;
            }
            if (!region.filled(x, y)) ++spilled;
        }
    }
    float const limit = static_cast<float>(area) * 0.05f;
    if (static_cast<float>(spilled) > limit || static_cast<float>(missing) > limit) return false;
    output.push_back(circle);
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
    auto covered = [&](float x, float y) {
        return std::any_of(shapes.begin(), shapes.end(), [&](Primitive const& shape) {
            return insideShape(shape, x, y);
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

    // Asomar sobre lo que otra capa tapa despues sale barato, pero no gratis: si
    // fuera gratis una capsula podria tragarse media imagen y seguir puntuando
    // perfecto, y luego el color de arriba no llega a taparla del todo y lo que
    // queda es una losa torcida atravesada en el dibujo.
    float spilled = 0.f;
    bool const hasBlocked = blocked.size() == target.size();
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if (target[index] || !covered(x + 0.5f, y + 0.5f)) continue;
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
    std::vector<std::uint8_t> const& blocked
) {
    if (positions.size() < 6) return false;
    std::vector<std::uint8_t> target(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) target[static_cast<std::size_t>(position)] = 1;
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
    auto consider = [&](std::vector<Primitive> const& shapes) {
        float const similarity = fitSimilarity(
            positions, target, width, height, shapes, blocked);
        if (similarity <= bestSimilarity) return;
        bestSimilarity = similarity;
        best = shapes;
    };
    for (int offset = -6; offset <= 6; ++offset) {
        float const angle = principal + offset * kPi / 180.f;
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
        float const support = 0.5f * (std::abs(cosine) + std::abs(sine));
        for (float lengthPadding : kLengthPadding) {
            for (float widthPadding : kWidthPadding) {
                float const totalLength = maxMajor - minMajor + support * 2.f * lengthPadding;
                float const diameter = maxMinor - minMinor + support * 2.f * widthPadding;
                if (totalLength / std::max(diameter, 0.01f) < 1.6f) continue;
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
                // Se prueba primero el rectangulo entero: si empata con la capsula
                // de puntas redondas gana el, que son dos objetos menos.
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
                bool const capped = appendRoundCap(
                        rounded, {center.x - extent.x, center.y - extent.y},
                        diameter, color, layer, width, height, blocked) &&
                    appendRoundCap(
                        rounded, {center.x + extent.x, center.y + extent.y},
                        diameter, color, layer, width, height, blocked);
                consider(squared);
                if (capped) consider(rounded);
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
    bool whole
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
                if (samples[index] == full) continue;
                for (std::size_t sample = 0; sample < sampleCount; ++sample) {
                    Point const point = whole ? kWholeSamples[sample] : Point{0.5f, 0.5f};
                    if (!insideShape(
                            object,
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
        covered[i] = samples[i] == full;
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
    std::vector<std::uint8_t> const& blocked
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
    // Lo que la figura girada ocupa de mas que las celdas que tiene que tapar. Una
    // mancha en ele o en ese cabe en su caja girada, pero la caja se lleva por
    // delante todo lo que hay en los huecos.
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
    // Una mota redonda se ve mejor como ovalo, siempre que no tenga que quedar
    // debajo de otro color: el objeto redondo no respeta el orden Z.
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
            shapeStaysInside(round, permitted, width, height)) {
            output.push_back(round);
            return true;
        }
    }

    if (!shapeStaysInside(patch, permitted, width, height)) return false;
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
    float const angle = object.rotation * kPi / 180.f;
    float const extentX = std::abs(std::cos(angle)) * object.width * 0.5f +
        std::abs(std::sin(angle)) * object.height * 0.5f;
    float const extentY = std::abs(std::sin(angle)) * object.width * 0.5f +
        std::abs(std::cos(angle)) * object.height * 0.5f;
    int const minX = std::max(0, static_cast<int>(std::floor(object.x - extentX)));
    int const minY = std::max(0, static_cast<int>(std::floor(object.y - extentY)));
    int const maxX = std::min(width - 1, static_cast<int>(std::ceil(object.x + extentX)));
    int const maxY = std::min(height - 1, static_cast<int>(std::ceil(object.y + extentY)));
    int count = 0;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            int const position = y * width + x;
            if (remaining[static_cast<std::size_t>(position)] &&
                insideShape(object, x + 0.5f, y + 0.5f)) {
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
    float const angle = object.rotation * kPi / 180.f;
    float const extentX = std::abs(std::cos(angle)) * object.width * 0.5f +
        std::abs(std::sin(angle)) * object.height * 0.5f;
    float const extentY = std::abs(std::sin(angle)) * object.width * 0.5f +
        std::abs(std::cos(angle)) * object.height * 0.5f;
    int const minX = std::max(0, static_cast<int>(std::floor(object.x - extentX)));
    int const minY = std::max(0, static_cast<int>(std::floor(object.y - extentY)));
    int const maxX = std::min(width - 1, static_cast<int>(std::ceil(object.x + extentX)));
    int const maxY = std::min(height - 1, static_cast<int>(std::ceil(object.y + extentY)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            int const position = y * width + x;
            if (remaining[static_cast<std::size_t>(position)] &&
                insideShape(object, x + 0.5f, y + 0.5f)) {
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
bool anySample(Primitive const& object, int width, int height, Test test) {
    auto const box = shapeBox(object, width, height);
    for (int y = box[1] * kPruneScale; y < (box[3] + 1) * kPruneScale; ++y) {
        for (int x = box[0] * kPruneScale; x < (box[2] + 1) * kPruneScale; ++x) {
            if (!insideShape(
                    object, (x + 0.5f) / kPruneScale, (y + 0.5f) / kPruneScale)) {
                continue;
            }
            if (test(static_cast<std::size_t>(y) * width * kPruneScale + x)) return true;
        }
    }
    return false;
}

// Un objeto sobra cuando no cambia el dibujo. De abajo arriba se sabe el color
// que ya hay pintado debajo, y el que solo repite ese mismo color no aporta nada:
// ahi caen los cuadrados de relleno que el trazo del mismo color ya tapaba, que
// eran casi la mitad de los objetos. De arriba abajo caen los que quedan
// enterrados del todo. Las dos pasadas dejan el dibujo identico.
void markUsefulObjects(std::vector<PruneEntry> entries, int width, int height) {
    std::stable_sort(entries.begin(), entries.end(), [](auto const& left, auto const& right) {
        return left.object->layer < right.object->layer;
    });
    std::size_t const samples =
        static_cast<std::size_t>(width) * height * kPruneScale * kPruneScale;

    std::vector<std::int16_t> painted(samples, -1);
    std::vector<std::uint8_t> useful(entries.size(), 0);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto const& object = *entries[index].object;
        auto const color = static_cast<std::int16_t>(object.color);
        if (!anySample(object, width, height, [&](std::size_t sample) {
                return painted[sample] != color;
            })) {
            continue;
        }
        useful[index] = 1;
        anySample(object, width, height, [&](std::size_t sample) {
            painted[sample] = color;
            return false;
        });
    }

    std::vector<std::uint8_t> covered(samples, 0);
    for (std::size_t index = entries.size(); index-- > 0;) {
        if (!useful[index]) continue;
        auto const& object = *entries[index].object;
        if (!anySample(object, width, height, [&](std::size_t sample) {
                return covered[sample] == 0;
            })) {
            continue;
        }
        *entries[index].keep = 1;
        anySample(object, width, height, [&](std::size_t sample) {
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

// Dos cuadrados rectos del mismo color que comparten un lado entero son un
// cuadrado mas grande. Se juntan aunque esten en capas distintas: entre objetos
// del mismo color el orden no cambia el dibujo, y cada color tiene su propio tramo
// de capas, asi que bajar el cuadrado a la capa mas baja de las dos no lo mete
// debajo de otro color. Hace falta porque el relleno y los remates de una misma
// mancha acaban en capas separadas y quedarian partidos.
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
        // Solo los que caen justo en la rejilla: los demas no se pueden sumar sin
        // mover el dibujo.
        if (std::abs(static_cast<float>(box.maxX - box.minX) - object.width) > 0.001f ||
            std::abs(static_cast<float>(box.maxY - box.minY) - object.height) > 0.001f) {
            continue;
        }
        boxes.push_back(box);
    }
    if (boxes.size() < 2) return;

    // La clave junta los que pueden sumarse: mismo color, mismo lado de la capa de
    // fondo y misma banda a lo ancho o a lo alto.
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

bool mergeRectPair(Primitive const& first, Primitive const& second, Primitive& result) {
    if (first.color != second.color || first.layer != second.layer ||
        (first.kind != PrimitiveKind::Block && first.kind != PrimitiveKind::Stroke) ||
        (second.kind != PrimitiveKind::Block && second.kind != PrimitiveKind::Stroke)) {
        return false;
    }

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
    if (mergedArea - unionArea > std::max(0.01f, unionArea * 0.002f)) return false;

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
        first.layer
    };
    return true;
}

void mergePaintRects(std::vector<Primitive>& objects) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t first = 0; first < objects.size() && !merged; ++first) {
            for (std::size_t second = first + 1; second < objects.size(); ++second) {
                Primitive replacement;
                if (!mergeRectPair(objects[first], objects[second], replacement)) continue;
                objects[first] = replacement;
                objects.erase(objects.begin() + static_cast<std::ptrdiff_t>(second));
                merged = true;
                break;
            }
        }
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
    std::vector<std::uint8_t> const& empty = {}
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
    // Asomar sobre un color que otra capa tapa despues no se ve, y sobre el hueco
    // tampoco hay nada que ensuciar: ahi es donde una diagonal puede rematarse
    // girada. Que no se estire por el hueco lo cuida el tope de tamano del parche.
    for (auto const* mask : {&blocked, &empty}) {
        if (mask->size() != cells) continue;
        for (std::size_t position = 0; position < cells; ++position) {
            permitted[position] |= (*mask)[position];
        }
    }

    // Por donde puede pasar un rectangulo sin cambiar el dibujo: las celdas de
    // este mismo color, que ya van pintadas igual, y las que otra capa tapa
    // despues. Atravesarlas es lo que junta el reguero de parches en uno solo.
    std::vector<std::uint8_t> spare(cells, 0);
    for (int position : paint) spare[static_cast<std::size_t>(position)] = 1;
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            spare[position] |= blocked[position];
        }
    }

    // Una escalera en diagonal no se puede empaquetar: cada peldano seria su propio
    // rectangulo. La tira girada se lleva tres o cuatro celdas de una tirada, y como
    // busca pareja por todo el color de una vez tambien enlaza peldanos de manchas
    // distintas. Lo que se quede sin pareja acaba de rectangulo.
    auto diagonalStrokes = [&](std::vector<int> const& group) {
        std::vector<Primitive> strokes;
        std::vector<int> unpaired;
        std::vector<std::uint8_t> remaining(cells, 0);
        for (int position : group) remaining[static_cast<std::size_t>(position)] = 1;
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
                    auto const candidate = repairStroke(first, second, width, color, layer);
                    if (!shapeStaysInside(candidate, permitted, width, height)) continue;
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
        // Una mancha que ya cabe en un rectangulo no necesita nada raro; se deja
        // para el empaquetado del final, que ademas puede juntarla con las de al
        // lado cruzando por celdas de este mismo color.
        auto rectangle = packBlocks(component, width, height, color, spare);
        // Girada cabe de una pieza: vale la pena cuando recta harian falta varias.
        if (rectangle.size() >= 2 && component.size() >= 4 &&
            appendSmallPatch(
                output, component, width, height, color, layer, permitted, blocked)) {
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

} // namespace

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
    if (interior.empty()) return repairs;

    auto problemCells = [&](std::vector<Primitive> const& visible, int scale) {
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
            float const angle = object->rotation * kPi / 180.f;
            float const extentX = std::abs(std::cos(angle)) * object->width * 0.5f +
                std::abs(std::sin(angle)) * object->height * 0.5f;
            float const extentY = std::abs(std::sin(angle)) * object->width * 0.5f +
                std::abs(std::cos(angle)) * object->height * 0.5f;
            int const minX = std::clamp(
                static_cast<int>(std::floor((object->x - extentX) * scale)),
                0, scaledWidth - 1);
            int const minY = std::clamp(
                static_cast<int>(std::floor((object->y - extentY) * scale)),
                0, scaledHeight - 1);
            int const maxX = std::clamp(
                static_cast<int>(std::ceil((object->x + extentX) * scale)),
                0, scaledWidth - 1);
            int const maxY = std::clamp(
                static_cast<int>(std::ceil((object->y + extentY) * scale)),
                0, scaledHeight - 1);
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    if (!insideShape(
                            *object, (x + 0.5f) / scale, (y + 0.5f) / scale)) {
                        continue;
                    }
                    std::size_t const sample =
                        static_cast<std::size_t>(y) * scaledWidth + x;
                    ownerColor[sample] = static_cast<std::int16_t>(object->color);
                    ownerLayer[sample] = object->layer;
                }
            }
        }

        std::vector<std::vector<int>> problems(ranks.size());
        for (int position : interior) {
            int const x = position % width;
            int const y = position / width;
            int const expected = cells[static_cast<std::size_t>(position)];
            bool problem = false;
            for (int sampleY = 0; sampleY < scale && !problem; ++sampleY) {
                for (int sampleX = 0; sampleX < scale; ++sampleX) {
                    std::size_t const sample =
                        static_cast<std::size_t>(y * scale + sampleY) * scaledWidth +
                        x * scale + sampleX;
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
    auto problems = problemCells(working, 8);
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
            object.layer = -1;
        }
        repairs.insert(repairs.end(), underpaint.begin(), underpaint.end());
        working.insert(working.end(), underpaint.begin(), underpaint.end());
        problems = problemCells(working, 12);
    }

    for (int color = 0; color < static_cast<int>(problems.size()); ++color) {
        auto patches = packBlocks(
            problems[static_cast<std::size_t>(color)], width, height, color);
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

    // Un objeto se queda si hace falta en algun frame, asi que las marcas se van
    // acumulando frame a frame.
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
        // Cada pista se fusiona por separado: dos cuadrados que no se encienden en
        // los mismos frames no son un cuadrado.
        mergePaintBlocks(tracks[track].objects);
        mergePaintRects(tracks[track].objects);
    }
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

    for (int color = 0; color < colors; ++color) {
        auto const& positions = masks[static_cast<std::size_t>(color)];
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
    int rank,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& empty
) {
    std::vector<Primitive> output;
    int const base = rank * kPaintSublayers;
    // Celdas que un rectangulo puede atravesar sin cambiar el dibujo: las de este
    // mismo color y las que otra capa tapa despues.
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
    if (positions.size() >= 8) {
        auto const region = buildRegion(positions, width);
        if (appendCircle(
                output, region, positions.size(), color, base,
                width, height, blocked) ||
            appendTriangle(output, positions, width, height, color, base, blocked) ||
            appendCapsule(output, positions, width, height, color, base + 1, blocked)) {
            appendRepairs(
                output,
                selectCells(
                    region, width, coverageMask(region, output, false), {}, true, 0.f),
                width, height, color, base + 2, blocked, positions, empty);
            return output;
        }
    }
    std::vector<std::vector<int>> pieces;
    for (auto const& whole : connectedComponents(positions, width, height)) {
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
        // Los bloques de una mancha son todos del mismo color, asi que el orden
        // entre ellos da igual: se juntan en una sola pasada de empaquetado para
        // que los rectangulos salgan lo mas grandes que se pueda.
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
            shapes, region, component.size(), color, base,
            width, height, blocked) ||
            appendTriangle(shapes, component, width, height, color, base, blocked) ||
            appendCapsule(shapes, component, width, height, color, base + 1, blocked);
        if (!fitted) {
            // La cadena de tiras solo vale para lo que de verdad es un trazo; si la
            // mancha resulta ser compacta el trazado se echa atras y no dibuja nada,
            // y entonces va por el contorno como cualquier mancha.
            chained = radius <= kThinRadius &&
                appendChain(
                    shapes, region, component, width, height, radius, color, base + 1,
                    blocked);
            if (!chained) {
                float const band = std::clamp(radius * 1.4f, 1.4f, kBandWidth);
                std::vector<Contour> refined;
                for (auto const& contour : traceContours(region)) {
                    if (contour.points.size() < 3) continue;
                    // Cuanto mas fina es la tira menos se la puede redondear: en
                    // una linea de dos celdas media celda de recorte ya es un
                    // cuarto del trazo y se ve como un pico.
                    float const local = contourThickness(region, contour, band);
                    refined.push_back(refineContour(
                        contour, local <= 2.5f ? 1 : 2,
                        std::clamp(local * 0.3f, 0.3f, kSmoothTolerance)));
                }
                std::vector<Primitive> outline;
                for (auto const& contour : refined) {
                    appendBand(
                        outline, region, contour, band, color, base + 1,
                        width, height, blocked);
                }
                inside = insideContours(region, refined);
                // El contorno suavizado se sale de la silueta en las curvas, y el
                // relleno lo sigue: donde se sale acaba pintando encima del color
                // de al lado, que es una mordida bien visible cuando este color va
                // por arriba. Fuera de la mascara solo se admite donde otro color
                // tapa despues o donde no hay nada que ensuciar.
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
                collectPlain(coverageMask(region, outline, true), inside, false, 0.f);
                shapes.insert(shapes.end(), outline.begin(), outline.end());
            }
        }

        collectPlain(
            coverageMask(region, shapes, false), inside, true, chained ? 1.1f : 1.6f);
        collectPlain(coverageMask(region, shapes, true), {}, true, 1.01f);
        appendBlocks(shapes, plain, width, height, color, base, spare);
        auto missing = selectCells(
            region, width, coverageMask(region, shapes, false), inside, true, 0.f);
        // Ni una celda suelta se puede dejar sin tapar: ahora el color de debajo
        // se estira por encima de las celdas que este tapa, asi que un hueco aqui
        // no deja transparencia sino el color de al lado. Salen baratas porque al
        // final se empaquetan todas juntas.
        repairs.insert(repairs.end(), missing.begin(), missing.end());
        output.insert(output.end(), shapes.begin(), shapes.end());
    }

    appendRepairs(
        output, repairs, width, height, color, base + 2, blocked, positions, empty);

    std::stable_sort(output.begin(), output.end(), [](Primitive const& left, Primitive const& right) {
        return left.layer < right.layer;
    });
    return output;
}

} // namespace paimon::gifimport
