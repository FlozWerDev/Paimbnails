#include "GifSmoothVectorizer.hpp"

#include "GifArtVectorizer.hpp"
#include "GifShapeRaster.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <utility>

namespace paimon::gifimport {
namespace {

float loopAreaOf(std::vector<Point> const& loop) {
    double total = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        auto const& a = loop[i];
        auto const& b = loop[(i + 1) % loop.size()];
        total += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return static_cast<float>(std::abs(total) * 0.5);
}

float signedAreaOf(std::vector<Point> const& loop) {
    double total = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        auto const& a = loop[i];
        auto const& b = loop[(i + 1) % loop.size()];
        total += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return static_cast<float>(total * 0.5);
}

// quita picos: vertice muy agudo con algun lado corto, y lados minusculos.
std::vector<Point> pruneSpikes(
    std::vector<Point> loop,
    float maxTurningDegrees = 58.f,
    float shortEdge = 1.6f,
    float minEdge = 0.9f
) {
    bool changed = true;
    while (changed && loop.size() > 3) {
        changed = false;
        std::size_t const n = loop.size();
        for (std::size_t i = 0; i < n; ++i) {
            Point const a = loop[(i + n - 1) % n];
            Point const b = loop[i];
            Point const c = loop[(i + 1) % n];
            float const e1 = pointDistance(a, b);
            float const e2 = pointDistance(b, c);
            float const vx1 = a.x - b.x;
            float const vy1 = a.y - b.y;
            float const vx2 = c.x - b.x;
            float const vy2 = c.y - b.y;
            float const n1 = std::max(std::hypot(vx1, vy1), 0.0001f);
            float const n2 = std::max(std::hypot(vx2, vy2), 0.0001f);
            float const cosine = std::clamp(
                (vx1 * vx2 + vy1 * vy2) / (n1 * n2), -1.f, 1.f);
            float const turning = 180.f - std::acos(cosine) * 180.f / kPi;
            if ((turning > maxTurningDegrees && std::min(e1, e2) < shortEdge) ||
                std::min(e1, e2) < minEdge) {
                loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    return loop;
}

// desplaza el bucle hacia fuera con inglete limitado: no crea picos nuevos.
std::vector<Point> expandLoop(std::vector<Point> const& loop, float distance) {
    std::size_t const n = loop.size();
    if (n < 3 || distance <= 0.f) return loop;
    auto offset = [&](float sign) {
        std::vector<Point> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            Point const prev = loop[(i + n - 1) % n];
            Point const cur = loop[i];
            Point const next = loop[(i + 1) % n];
            float const e1x = cur.x - prev.x;
            float const e1y = cur.y - prev.y;
            float const e2x = next.x - cur.x;
            float const e2y = next.y - cur.y;
            float const l1 = std::max(std::hypot(e1x, e1y), 0.0001f);
            float const l2 = std::max(std::hypot(e2x, e2y), 0.0001f);
            float mx = e1y / l1 + e2y / l2;
            float my = -e1x / l1 + -e2x / l2;
            float const lm = std::hypot(mx, my);
            if (lm < 0.000001f) {
                out.push_back(cur);
                continue;
            }
            mx /= lm;
            my /= lm;
            float const cosHalf = std::max(0.35f, lm * 0.5f);
            float step = sign * distance / cosHalf;
            step = std::clamp(step, -2.5f * distance, 2.5f * distance);
            out.push_back({cur.x + mx * step, cur.y + my * step});
        }
        return out;
    };
    auto grown = offset(1.f);
    if (loopAreaOf(grown) < loopAreaOf(loop)) grown = offset(-1.f);
    return grown;
}

std::vector<Point> smoothOneLoop(
    std::vector<Point> const& loop,
    int componentArea,
    int width,
    int height
) {
    float const area = loopAreaOf(loop);
    float eps1 = area >= 5000.f ? 1.35f : area >= 800.f ? 0.98f
        : area >= 150.f ? 0.64f : 0.5f;
    float eps2 = area >= 5000.f ? 0.64f : area >= 800.f ? 0.5f
        : area >= 150.f ? 0.34f : 0.28f;
    if (componentArea < 2500) {
        eps1 = std::min(eps1, 0.65f);
        eps2 = std::min(eps2, 0.35f);
    }
    bool const touchesBorder = std::any_of(loop.begin(), loop.end(), [&](Point const& p) {
        return p.x <= 0.01f || p.y <= 0.01f ||
            p.x >= static_cast<float>(width) - 0.01f ||
            p.y >= static_cast<float>(height) - 0.01f;
    });
    if (touchesBorder) {
        // el borde del lienzo debe seguir siendo borde: sin Chaikin ni escala.
        auto pts = pruneSpikes(simplifyLoop(loop, 0.45f), 58.f, 1.2f);
        for (auto& p : pts) {
            p.x = std::clamp(p.x, 0.f, static_cast<float>(width));
            p.y = std::clamp(p.y, 0.f, static_cast<float>(height));
        }
        return pts;
    }
    auto pts = simplifyLoop(loop, eps1);
    if (pts.size() >= 5 && area >= 60.f) {
        pts = smoothLoop(pts, 1);
        pts = simplifyLoop(pts, eps2);
    }
    pts = pruneSpikes(pts, 58.f, area >= 800.f ? 2.4f : 1.6f);
    // chaikin recorta esquinas y encoge el bucle: restaurar su area.
    if (pts.size() >= 3 && area >= 60.f) {
        float const smoothed = loopAreaOf(pts);
        if (smoothed > 1.f) {
            float const factor = std::sqrt(area / smoothed);
            if (factor >= 0.97f && factor <= 1.06f && std::abs(factor - 1.f) > 0.002f) {
                Point centroid;
                for (auto const& p : pts) {
                    centroid.x += p.x;
                    centroid.y += p.y;
                }
                centroid.x /= static_cast<float>(pts.size());
                centroid.y /= static_cast<float>(pts.size());
                for (auto& p : pts) {
                    p.x = centroid.x + (p.x - centroid.x) * factor;
                    p.y = centroid.y + (p.y - centroid.y) * factor;
                }
            }
        }
    }
    return pts;
}

// bucles cerrados por encadenado de aristas con el interior a un lado; en
// sillas se gira a la derecha para no fusionar bucles que se tocan.
std::vector<std::vector<Point>> traceLoops(
    std::vector<std::uint8_t> const& mask,
    int width,
    int height,
    std::vector<int> const& cells
) {
    int const stride = width + 1;
    auto pointId = [&](int x, int y) { return y * stride + x; };
    std::unordered_map<int, std::vector<int>> edges;
    auto addEdge = [&](int from, int to) { edges[from].push_back(to); };
    auto filled = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
            mask[static_cast<std::size_t>(y) * width + x];
    };
    for (int position : cells) {
        int const x = position % width;
        int const y = position / width;
        if (!filled(x, y - 1)) addEdge(pointId(x, y), pointId(x + 1, y));
        if (!filled(x + 1, y)) addEdge(pointId(x + 1, y), pointId(x + 1, y + 1));
        if (!filled(x, y + 1)) addEdge(pointId(x + 1, y + 1), pointId(x, y + 1));
        if (!filled(x - 1, y)) addEdge(pointId(x, y + 1), pointId(x, y));
    }
    std::vector<std::vector<Point>> loops;
    while (!edges.empty()) {
        auto startIt = edges.begin();
        int const start = startIt->first;
        int next = startIt->second.front();
        startIt->second.erase(startIt->second.begin());
        if (startIt->second.empty()) edges.erase(startIt);
        std::vector<int> walk{start, next};
        int prev = start;
        int cur = next;
        std::size_t guard = 0;
        while (cur != start && guard++ < 400000) {
            auto it = edges.find(cur);
            if (it == edges.end() || it->second.empty()) break;
            int const dx = cur % stride - prev % stride;
            int const dy = cur / stride - prev / stride;
            // preferencia: derecha, recto, izquierda, atras (coords y-down)
            std::array<std::pair<int, int>, 4> const want{{
                {-dy, dx}, {dx, dy}, {dy, -dx}, {-dx, -dy}}};
            int pick = -1;
            for (auto const& dir : want) {
                int const candidate = pointId(
                    cur % stride + dir.first, cur / stride + dir.second);
                if (std::find(it->second.begin(), it->second.end(), candidate) !=
                    it->second.end()) {
                    pick = candidate;
                    break;
                }
            }
            if (pick < 0) break;
            it->second.erase(std::find(it->second.begin(), it->second.end(), pick));
            if (it->second.empty()) edges.erase(it);
            walk.push_back(pick);
            prev = cur;
            cur = pick;
        }
        if (walk.size() >= 4 && walk.back() == walk.front()) {
            walk.pop_back();
            std::vector<Point> loop;
            loop.reserve(walk.size());
            for (int id : walk) {
                loop.push_back({
                    static_cast<float>(id % stride),
                    static_cast<float>(id / stride)});
            }
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

// celdas interiores (erosionadas 1) de un bucle, por barrido par-impar.
std::vector<int> holeInteriorCells(
    std::vector<Point> const& loop,
    int width,
    int height
) {
    float minY = loop.front().y;
    float maxY = loop.front().y;
    for (auto const& p : loop) {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    std::vector<std::uint8_t> filled(static_cast<std::size_t>(width) * height, 0);
    int const yFrom = std::max(0, static_cast<int>(std::floor(minY)));
    int const yTo = std::min(height - 1, static_cast<int>(std::ceil(maxY)));
    std::vector<float> crossings;
    for (int y = yFrom; y <= yTo; ++y) {
        float const scanY = static_cast<float>(y) + 0.5f;
        crossings.clear();
        for (std::size_t i = 0; i < loop.size(); ++i) {
            Point const a = loop[i];
            Point const b = loop[(i + 1) % loop.size()];
            if (a.y == b.y) continue;
            float const lo = std::min(a.y, b.y);
            float const hi = std::max(a.y, b.y);
            if (scanY < lo || scanY >= hi) continue;
            float const t = (scanY - a.y) / (b.y - a.y);
            crossings.push_back(a.x + t * (b.x - a.x));
        }
        std::sort(crossings.begin(), crossings.end());
        for (std::size_t k = 0; k + 1 < crossings.size(); k += 2) {
            int const xFrom = std::max(0, static_cast<int>(std::ceil(crossings[k] - 0.5f)));
            int const xTo = std::min(width - 1,
                static_cast<int>(std::floor(crossings[k + 1] - 0.5f)));
            for (int x = xFrom; x <= xTo; ++x)
                filled[static_cast<std::size_t>(y) * width + x] = 1;
        }
    }
    std::vector<int> interior;
    for (int y = 1; y + 1 < height; ++y) {
        for (int x = 1; x + 1 < width; ++x) {
            std::size_t const i = static_cast<std::size_t>(y) * width + x;
            if (filled[i] && filled[i - 1] && filled[i + 1] &&
                filled[i - width] && filled[i + width])
                interior.push_back(static_cast<int>(i));
        }
    }
    return interior;
}

// fuera huecos cuyo interior repinta por completo un color posterior.
std::vector<std::vector<Point>> dropRepaintedHoles(
    std::vector<std::vector<Point>> const& loops,
    std::vector<std::uint8_t> const& coveredLater,
    int width,
    int height
) {
    if (loops.size() <= 1 || coveredLater.empty()) return loops;
    std::vector<float> signs;
    signs.reserve(loops.size());
    std::size_t outer = 0;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        signs.push_back(signedAreaOf(loops[i]));
        if (std::abs(signs.back()) > std::abs(signs[outer])) outer = i;
    }
    float const outerSign = signs[outer] >= 0.f ? 1.f : -1.f;
    std::vector<std::vector<Point>> kept;
    for (std::size_t i = 0; i < loops.size(); ++i) {
        if (signs[i] * outerSign < 0.f && std::abs(signs[i]) > 4.f) {
            auto const interior = holeInteriorCells(loops[i], width, height);
            bool const repainted = !interior.empty() && std::all_of(
                interior.begin(), interior.end(), [&](int cell) {
                    return coveredLater[static_cast<std::size_t>(cell)] != 0;
                });
            if (repainted) continue;
        }
        kept.push_back(loops[i]);
    }
    return kept.empty() ? loops : kept;
}

struct Trap {
    float y0 = 0.f;
    float y1 = 0.f;
    float lt = 0.f;
    float rt = 0.f;
    float lb = 0.f;
    float rb = 0.f;
};

std::vector<Trap> slabTrapezoids(std::vector<std::vector<Point>> const& loops) {
    std::vector<float> ys;
    for (auto const& loop : loops)
        for (auto const& p : loop) ys.push_back(p.y);
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    struct Edge { Point a; Point b; };
    std::vector<Edge> edges;
    for (auto const& loop : loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            Point const a = loop[i];
            Point const b = loop[(i + 1) % loop.size()];
            if (a.y != b.y) edges.push_back({a, b});
        }
    }
    std::vector<Trap> traps;
    std::vector<std::array<float, 3>> cross;
    for (std::size_t yi = 0; yi + 1 < ys.size(); ++yi) {
        float const y0 = ys[yi];
        float const y1 = ys[yi + 1];
        if (y1 - y0 < 0.000001f) continue;
        float const ym = (y0 + y1) * 0.5f;
        cross.clear();
        for (auto const& e : edges) {
            float const lo = std::min(e.a.y, e.b.y);
            float const hi = std::max(e.a.y, e.b.y);
            if (lo > y0 + 0.000001f || hi < y1 - 0.000001f) continue;
            float const span = e.b.y - e.a.y;
            cross.push_back({
                e.a.x + (ym - e.a.y) / span * (e.b.x - e.a.x),
                e.a.x + (y0 - e.a.y) / span * (e.b.x - e.a.x),
                e.a.x + (y1 - e.a.y) / span * (e.b.x - e.a.x)});
        }
        std::sort(cross.begin(), cross.end(),
                  [](auto const& a, auto const& b) { return a[0] < b[0]; });
        for (std::size_t k = 0; k + 1 < cross.size(); k += 2) {
            Trap trap{y0, y1, cross[k][1], cross[k + 1][1], cross[k][2], cross[k + 1][2]};
            if (std::min(trap.rt - trap.lt, trap.rb - trap.lb) > 0.05f)
                traps.push_back(trap);
        }
    }
    // fusion vertical de trapecios con bordes casi colineales
    std::map<float, std::vector<std::size_t>> byTop;
    for (std::size_t i = 0; i < traps.size(); ++i) byTop[traps[i].y0].push_back(i);
    std::vector<std::uint8_t> visited(traps.size(), 0);
    std::vector<Trap> merged;
    for (std::size_t i = 0; i < traps.size(); ++i) {
        if (visited[i]) continue;
        visited[i] = 1;
        Trap cur = traps[i];
        while (true) {
            int next = -1;
            auto const it = byTop.find(cur.y1);
            if (it != byTop.end()) {
                for (std::size_t j : it->second) {
                    if (visited[j]) continue;
                    Trap const& o = traps[j];
                    if (std::abs(o.lt - cur.lb) >= 0.13f ||
                        std::abs(o.rt - cur.rb) >= 0.13f) continue;
                    float const h1 = std::max(cur.y1 - cur.y0, 0.0001f);
                    float const h2 = std::max(o.y1 - o.y0, 0.0001f);
                    if (std::abs((cur.lb - cur.lt) / h1 - (o.lb - o.lt) / h2) >= 0.10f)
                        continue;
                    if (std::abs((cur.rb - cur.rt) / h1 - (o.rb - o.rt) / h2) >= 0.10f)
                        continue;
                    next = static_cast<int>(j);
                    break;
                }
            }
            if (next < 0) break;
            visited[static_cast<std::size_t>(next)] = 1;
            Trap const& o = traps[static_cast<std::size_t>(next)];
            cur.y1 = o.y1;
            cur.lb = o.lb;
            cur.rb = o.rb;
        }
        if (cur.y1 - cur.y0 > 0.05f) merged.push_back(cur);
    }
    return merged;
}

void appendSmoothTriangle(
    std::vector<Primitive>& out, Point a, Point b, Point c,
    std::uint16_t color, std::int16_t layer
) {
    float const cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs(cross) < 0.001f) return;
    if (cross < 0.f) std::swap(b, c);
    float const xAngle = std::atan2(b.y - a.y, b.x - a.x);
    float const yAngle = std::atan2(c.y - a.y, c.x - a.x);
    Primitive object{
        (b.x + c.x) * 0.5f, (b.y + c.y) * 0.5f,
        pointDistance(a, b), pointDistance(a, c),
        xAngle * 180.f / kPi,
        color, PrimitiveKind::Triangle, layer, 0};
    object.skew = (yAngle - xAngle) * 180.f / kPi - 90.f;
    out.push_back(object);
}

void appendSmoothBand(
    std::vector<Primitive>& out, Trap const& trap,
    std::uint16_t color, std::int16_t layer
) {
    float const overlap = 0.22f;
    float const h = trap.y1 - trap.y0;
    if (h <= 0.f) return;
    float const sl = (trap.lb - trap.lt) / h;
    float const sr = (trap.rb - trap.rt) / h;
    float const yt = trap.y0 - overlap;
    float const yb = trap.y1 + overlap;
    float lt = trap.lt - sl * overlap;
    float rt = trap.rt - sr * overlap;
    float lb = trap.lb + sl * overlap;
    float rb = trap.rb + sr * overlap;
    float const lo = std::min(trap.lt, trap.lb) - 0.35f;
    float const hi = std::max(trap.rt, trap.rb) + 0.35f;
    lt = std::max(lt, lo);
    rt = std::min(rt, hi);
    lb = std::max(lb, lo);
    rb = std::min(rb, hi);
    float const topW = rt - lt;
    float const botW = rb - lb;
    float const w = std::min(topW, botW);
    if (w <= 0.05f) return;
    float const dy = yb - yt;
    float const dx = lb - lt;
    // skew extremo: las bases 131/132 casi paralelas deforman la textura;
    // como el rechazo boundedOutline del convertidor, cae a bloques rectos.
    if (std::abs(std::atan2(dx, dy)) * 180.f / kPi > 70.f && h > 0.7f) {
        int const steps = std::max(2, static_cast<int>(std::ceil(h / 0.6f)));
        for (int s = 0; s < steps; ++s) {
            float const ya = trap.y0 + h * s / steps;
            float const yb2 = trap.y0 + h * (s + 1) / steps;
            float const ta = (ya - trap.y0) / h;
            float const tb = (yb2 - trap.y0) / h;
            float const la = trap.lt + (trap.lb - trap.lt) * ta;
            float const ra = trap.rt + (trap.rb - trap.rt) * ta;
            float const lbn = trap.lt + (trap.lb - trap.lt) * tb;
            float const rbn = trap.rt + (trap.rb - trap.rt) * tb;
            out.push_back({
                (la + lbn + ra + rbn) * 0.25f, (ya + yb2) * 0.5f,
                std::max(ra - la, rbn - lbn), (yb2 - ya) + 0.12f, 0.f,
                color, PrimitiveKind::Block, layer, 0});
        }
        return;
    }
    Primitive body{
        (lt + lb + w) * 0.5f, (yt + yb) * 0.5f, w, std::hypot(dx, dy), 0.f,
        color, PrimitiveKind::Block, layer, 0};
    body.skew = -std::atan2(dx, dy) * 180.f / kPi;
    out.push_back(body);
    if (topW > botW + 0.001f) {
        appendSmoothTriangle(out, {lt + w, yt}, {rt, yt}, {rb, yb}, color, layer);
    } else if (botW > topW + 0.001f) {
        appendSmoothTriangle(out, {rt, yt}, {lb + w, yb}, {rb, yb}, color, layer);
    }
}

struct FitLine {
    float slope = 0.f;
    float intercept = 0.f;
    float at(float y) const { return slope * y + intercept; }
};

FitLine fitLineOf(std::vector<std::pair<float, float>> const& samples) {
    float meanY = 0.f;
    float meanX = 0.f;
    for (auto const& s : samples) {
        meanY += s.first;
        meanX += s.second;
    }
    meanY /= static_cast<float>(samples.size());
    meanX /= static_cast<float>(samples.size());
    float var = 0.f;
    float cov = 0.f;
    for (auto const& s : samples) {
        var += (s.first - meanY) * (s.first - meanY);
        cov += (s.first - meanY) * (s.second - meanX);
    }
    if (var <= 0.000001f) return {0.f, meanX};
    float const slope = cov / var;
    return {slope, meanX - slope * meanY};
}

// bandas largas sobre los bordes suavizados: flujos con padre/hijo unicos
// y rectas por minimos cuadrados que siempre cubren el borde (como
// preservesVisibleCenters), con junta compartida entre bandas vecinas.
std::vector<Trap> bandsFromLoops(
    std::vector<std::vector<Point>> const& loops,
    float tolerance
) {
    auto traps = slabTrapezoids(loops);
    if (traps.empty()) return {};
    std::sort(traps.begin(), traps.end(), [](Trap const& a, Trap const& b) {
        return a.y0 != b.y0 ? a.y0 < b.y0 : a.lt < b.lt;
    });
    std::vector<std::vector<Trap>> streams;
    std::map<float, std::vector<std::size_t>> levels;
    for (std::size_t i = 0; i < traps.size(); ++i) levels[traps[i].y0].push_back(i);
    std::unordered_map<std::size_t, std::size_t> lastTrap; // stream idx -> trap idx
    for (auto const& [top, group] : levels) {
        (void)top;
        std::unordered_map<std::size_t, std::vector<std::size_t>> parents;
        std::unordered_map<std::size_t, std::vector<std::size_t>> children;
        for (std::size_t ti : group) {
            for (auto const& [stream, lastIdx] : lastTrap) {
                Trap const& last = traps[lastIdx];
                if (std::abs(last.y1 - traps[ti].y0) > 0.000001f) continue;
                float const overlap = std::min(last.rb, traps[ti].rt) -
                    std::max(last.lb, traps[ti].lt);
                if (overlap > 0.3f * std::min(last.rb - last.lb, traps[ti].rt - traps[ti].lt)) {
                    parents[ti].push_back(stream);
                    children[stream].push_back(ti);
                }
            }
        }
        for (std::size_t ti : group) {
            auto const& cand = parents[ti];
            if (cand.size() == 1 && children[cand.front()].size() == 1) {
                std::size_t const stream = cand.front();
                streams[stream].push_back(traps[ti]);
                lastTrap[stream] = ti;
            } else {
                streams.push_back({traps[ti]});
                lastTrap[streams.size() - 1] = ti;
            }
        }
    }
    std::vector<Trap> bands;
    for (auto& stream : streams) {
        std::sort(stream.begin(), stream.end(),
                  [](Trap const& a, Trap const& b) { return a.y0 < b.y0; });
        std::vector<Trap> own;
        std::size_t i = 0;
        while (i < stream.size()) {
            int best = -1;
            FitLine bestLeft{};
            FitLine bestRight{};
            std::vector<std::pair<float, float>> bestLSamples;
            std::vector<std::pair<float, float>> bestRSamples;
            for (std::size_t j = i; j < stream.size(); ++j) {
                std::vector<std::pair<float, float>> lsamples;
                std::vector<std::pair<float, float>> rsamples;
                for (std::size_t k = i; k <= j; ++k) {
                    lsamples.emplace_back(stream[k].y0, stream[k].lt);
                    lsamples.emplace_back(stream[k].y1, stream[k].lb);
                    rsamples.emplace_back(stream[k].y0, stream[k].rt);
                    rsamples.emplace_back(stream[k].y1, stream[k].rb);
                }
                FitLine const ll = fitLineOf(lsamples);
                FitLine const rl = fitLineOf(rsamples);
                float dev = 0.f;
                for (auto const& s : lsamples)
                    dev = std::max(dev, std::abs(ll.at(s.first) - s.second));
                for (auto const& s : rsamples)
                    dev = std::max(dev, std::abs(rl.at(s.first) - s.second));
                float const top = stream[i].y0;
                float const bottom = stream[j].y1;
                if (rl.at(top) - ll.at(top) <= 0.05f ||
                    rl.at(bottom) - ll.at(bottom) <= 0.05f) break;
                if (dev > tolerance) break;
                best = static_cast<int>(j);
                bestLeft = ll;
                bestRight = rl;
                bestLSamples = std::move(lsamples);
                bestRSamples = std::move(rsamples);
            }
            if (best < 0) {
                own.push_back(stream[i]);
                ++i;
            } else {
                // cubrir siempre: desplazar cada recta hacia fuera.
                float shiftL = 0.f;
                float shiftR = 0.f;
                for (auto const& s : bestLSamples)
                    shiftL = std::min(shiftL, s.second - bestLeft.at(s.first));
                for (auto const& s : bestRSamples)
                    shiftR = std::max(shiftR, s.second - bestRight.at(s.first));
                bestLeft.intercept += shiftL;
                bestRight.intercept += shiftR;
                float const top = stream[i].y0;
                float const bottom = stream[static_cast<std::size_t>(best)].y1;
                own.push_back({top, bottom, bestLeft.at(top), bestRight.at(top),
                               bestLeft.at(bottom), bestRight.at(bottom)});
                i = static_cast<std::size_t>(best) + 1;
            }
        }
        // junta compartida entre bandas consecutivas del mismo flujo.
        for (std::size_t k = 0; k + 1 < own.size(); ++k) {
            Trap& a = own[k];
            Trap& b = own[k + 1];
            if (std::abs(a.y1 - b.y0) > 0.000001f) continue;
            float const left = (a.lb + b.lt) * 0.5f;
            float const right = (a.rb + b.rt) * 0.5f;
            a.lb = left;
            a.rb = right;
            b.lt = left;
            b.rt = right;
        }
        bands.insert(bands.end(), own.begin(), own.end());
    }
    return bands;
}

// regiones casi circulares: un solo circulo por momentos (nieve, brillos).
bool fitSmoothCircle(
    std::vector<int> const& cells,
    std::vector<std::uint8_t> const& mask,
    int width,
    int height,
    std::uint16_t color,
    std::int16_t layer,
    Primitive& out
) {
    if (cells.size() < 9) return false;
    double cx = 0.0;
    double cy = 0.0;
    for (int position : cells) {
        cx += position % width + 0.5;
        cy += position / width + 0.5;
    }
    cx /= static_cast<double>(cells.size());
    cy /= static_cast<double>(cells.size());
    double xx = 0.0;
    double xy = 0.0;
    double yy = 0.0;
    std::size_t perimeter = 0;
    auto filled = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
            mask[static_cast<std::size_t>(y) * width + x];
    };
    for (int position : cells) {
        int const x = position % width;
        int const y = position / width;
        double const dx = x + 0.5 - cx;
        double const dy = y + 0.5 - cy;
        xx += dx * dx;
        xy += dx * dy;
        yy += dy * dy;
        perimeter += static_cast<std::size_t>(!filled(x, y - 1) + !filled(x + 1, y) +
                                              !filled(x, y + 1) + !filled(x - 1, y));
    }
    double const n = static_cast<double>(cells.size());
    xx /= n;
    xy /= n;
    yy /= n;
    double const spread = std::hypot(xx - yy, 2.0 * xy);
    double const major = 4.0 * std::sqrt(std::max(0.0, (xx + yy + spread) * 0.5));
    double const minor = 4.0 * std::sqrt(std::max(0.0, (xx + yy - spread) * 0.5));
    if (minor < 2.0 || major > minor * 2.2) return false;
    double const ellipseArea = major * minor * kPi * 0.25;
    if (ellipseArea < 0.72 * n || ellipseArea > 1.35 * n) return false;
    double const circularity = 4.0 * kPi * n /
        std::max<double>(static_cast<double>(perimeter * perimeter), 1.0);
    if (circularity < 0.62) return false;
    out = {
        static_cast<float>(cx), static_cast<float>(cy),
        static_cast<float>(major), static_cast<float>(minor),
        static_cast<float>(0.5 * std::atan2(2.0 * xy, xx - yy) * 180.0 / kPi),
        color, PrimitiveKind::Circle, layer, 0};
    return true;
}

} // namespace

std::vector<Primitive> vectorizeSmoothPaint(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int layer,
    std::vector<std::uint8_t> const& coveredLater
) {
    std::vector<Primitive> output;
    if (positions.empty() || width <= 0 || height <= 0) return output;
    auto const ownColor = static_cast<std::uint16_t>(color);
    auto const ownLayer = static_cast<std::int16_t>(layer);

    auto const components = connectedComponents(positions, width, height);
    std::vector<std::uint8_t> componentMask(static_cast<std::size_t>(width) * height, 0);
    std::vector<std::uint8_t> covered(static_cast<std::size_t>(width) * height, 0);
    for (auto const& cells : components) {
        if (cells.empty()) continue;
        int const area = static_cast<int>(cells.size());
        std::size_t const componentStart = output.size();
        for (int position : cells) componentMask[static_cast<std::size_t>(position)] = 1;

        Primitive circle{};
        bool const asCircle = fitSmoothCircle(
            cells, componentMask, width, height, ownColor, ownLayer, circle);
        if (asCircle) output.push_back(circle);
        if (!asCircle) {
            float const expand = area >= 800 ? 0.3f : area >= 150 ? 0.25f : 0.05f;
            float const tolerance = area >= 800 ? 1.1f : area >= 150 ? 0.8f : 0.5f;
            auto loops = traceLoops(componentMask, width, height, cells);
            std::vector<std::vector<Point>> smoothed;
            for (auto const& loop : loops) {
                auto pts = smoothOneLoop(loop, area, width, height);
                if (pts.size() >= 3) smoothed.push_back(std::move(pts));
            }
            smoothed = dropRepaintedHoles(smoothed, coveredLater, width, height);
            std::vector<std::vector<Point>> expanded;
            for (auto const& loop : smoothed) {
                auto pts = expandLoop(loop, expand);
                for (auto& p : pts) {
                    p.x = std::clamp(p.x, 0.f, static_cast<float>(width));
                    p.y = std::clamp(p.y, 0.f, static_cast<float>(height));
                }
                if (pts.size() >= 3) expanded.push_back(std::move(pts));
            }
            std::size_t const beforeBands = output.size();
            for (auto const& trap : bandsFromLoops(expanded, tolerance))
                appendSmoothBand(output, trap, ownColor, ownLayer);
            if (output.size() == beforeBands) {
                auto packed = packBlocks(cells, width, height, color, coveredLater);
                for (auto& object : packed) object.layer = ownLayer;
                output.insert(output.end(), packed.begin(), packed.end());
            }
        }

        // reparacion interior: celdas propias profundas que el suavizado
        // dejo sin cubrir se tapan con rectangulos (como repairInterior).
        std::fill(covered.begin(), covered.end(), 0);
        for (std::size_t i = componentStart; i < output.size(); ++i) {
            auto const shape = xformOf(output[i]);
            auto const box = xformBox(shape, width, height);
            for (int y = box[1]; y <= box[3]; ++y) {
                for (int x = box[0]; x <= box[2]; ++x) {
                    if (shape.contains(static_cast<float>(x) + 0.5f,
                                       static_cast<float>(y) + 0.5f))
                        covered[static_cast<std::size_t>(y) * width + x] = 1;
                }
            }
        }
        std::vector<int> missing;
        for (int position : cells) {
            if (covered[static_cast<std::size_t>(position)]) continue;
            int const x = position % width;
            int const y = position / width;
            bool const deep = x > 0 && y > 0 && x + 1 < width && y + 1 < height &&
                componentMask[static_cast<std::size_t>(position) - 1] &&
                componentMask[static_cast<std::size_t>(position) + 1] &&
                componentMask[static_cast<std::size_t>(position) - width] &&
                componentMask[static_cast<std::size_t>(position) + width];
            if (deep) missing.push_back(position);
        }
        if (!missing.empty()) {
            auto packed = packBlocks(missing, width, height, color, coveredLater);
            for (auto& object : packed) object.layer = ownLayer;
            output.insert(output.end(), packed.begin(), packed.end());
        }
        for (int position : cells) componentMask[static_cast<std::size_t>(position)] = 0;
    }
    return output;
}

} // namespace paimon::gifimport


