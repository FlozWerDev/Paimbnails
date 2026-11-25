#include "GifVectorMath.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>
#include <utility>

namespace paimon::gifimport {

namespace {

std::vector<Point> subdivide(std::vector<Point> const& current, bool closed) {
    std::vector<Point> next;
    if (current.empty()) return next;
    next.reserve(current.size() * 2 + 2);
    if (!closed) next.push_back(current.front());
    std::size_t const segments = closed ? current.size() : current.size() - 1;
    for (std::size_t i = 0; i < segments; ++i) {
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
    if (!closed) next.push_back(current.back());
    return next;
}

int skeletonNeighbors(Skeleton const& skeleton, int position, std::array<int, 8>& output,
    bool pruneCornerShortcuts) {
    int const x = position % skeleton.width;
    int const y = position / skeleton.width;
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int const xx = x + dx;
            int const yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= skeleton.width || yy >= skeleton.height) continue;
            // A diagonal shortcut across an occupied right-angle corner makes
            // a triangle in the graph and invents a three-way junction.
            if (pruneCornerShortcuts && dx != 0 && dy != 0 &&
                (skeleton.cells[static_cast<std::size_t>(y) * skeleton.width + xx] ||
                 skeleton.cells[static_cast<std::size_t>(yy) * skeleton.width + x])) continue;
            int const next = yy * skeleton.width + xx;
            if (skeleton.cells[static_cast<std::size_t>(next)]) output[count++] = next;
        }
    }
    return count;
}

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

std::uint64_t edgeKey(int first, int second) {
    auto const low = static_cast<std::uint32_t>(std::min(first, second));
    auto const high = static_cast<std::uint32_t>(std::max(first, second));
    return (static_cast<std::uint64_t>(low) << 32) | high;
}

} // namespace

float pointDistance(Point const& first, Point const& second) {
    return std::hypot(second.x - first.x, second.y - first.y);
}

float lineDistance(Point const& point, Point const& first, Point const& last) {
    float const dx = last.x - first.x;
    float const dy = last.y - first.y;
    float const lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0001f) return pointDistance(point, first);
    float const projection = std::clamp(
        ((point.x - first.x) * dx + (point.y - first.y) * dy) / lengthSquared, 0.f, 1.f);
    return std::hypot(
        point.x - (first.x + projection * dx),
        point.y - (first.y + projection * dy));
}

std::vector<Point> simplify(std::vector<Point> const& points, float tolerance) {
    if (points.size() <= 2) return points;
    std::vector<std::uint8_t> keep(points.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> pending{
        {0, points.size() - 1}
    };
    while (!pending.empty()) {
        auto const [first, last] = pending.back();
        pending.pop_back();
        float farthest = 0.f;
        std::size_t split = first;
        for (std::size_t i = first + 1; i < last; ++i) {
            float const distance = lineDistance(points[i], points[first], points[last]);
            if (distance > farthest) {
                farthest = distance;
                split = i;
            }
        }
        if (farthest <= tolerance) continue;
        keep[split] = 1;
        pending.emplace_back(first, split);
        pending.emplace_back(split, last);
    }
    std::vector<Point> output;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) output.push_back(points[i]);
    }
    return output;
}

std::vector<Point> smoothLoop(std::vector<Point> const& loop, int iterations) {
    std::vector<Point> current = loop;
    for (int pass = 0; pass < iterations; ++pass) {
        current = subdivide(current, true);
    }
    return current;
}

std::vector<Point> smoothPath(std::vector<Point> const& path, int iterations) {
    std::vector<Point> current = path;
    for (int pass = 0; pass < iterations && current.size() > 2; ++pass) {
        current = subdivide(current, false);
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

std::vector<Point> smoothAndSimplify(
    std::vector<Point> const& points,
    bool closed,
    int iterations,
    float tolerance
) {
    return closed
        ? simplifyLoop(smoothLoop(points, iterations), tolerance)
        : simplify(smoothPath(points, iterations), tolerance);
}

std::vector<float> distanceField(
    std::vector<std::uint8_t> const& cells,
    int width,
    int height
) {
    constexpr float kInfinity = 1e18f;
    std::size_t const total = static_cast<std::size_t>(width) * height;
    std::vector<float> distance(total, 0.f);
    std::vector<float> work(total, 0.f);
    for (std::size_t i = 0; i < total; ++i) work[i] = cells[i] ? kInfinity : 0.f;

    int const span = std::max(width, height);
    std::vector<float> source(static_cast<std::size_t>(span));
    std::vector<float> target(static_cast<std::size_t>(span));
    std::vector<int> hull(static_cast<std::size_t>(span));
    std::vector<float> breaks(static_cast<std::size_t>(span) + 1);

    // separable EDT: gather each line, run the 1D transform, scatter back.
    auto pass = [&](int lines, int lineLen, auto&& index, auto&& store) {
        for (int line = 0; line < lines; ++line) {
            for (int i = 0; i < lineLen; ++i) {
                source[static_cast<std::size_t>(i)] = work[index(line, i)];
            }
            transformRow(source, target, hull, breaks, lineLen);
            for (int i = 0; i < lineLen; ++i) {
                store(line, i, target[static_cast<std::size_t>(i)]);
            }
        }
    };
    auto at = [&](int y, int x) { return static_cast<std::size_t>(y) * width + x; };
    pass(width, height,
        [&](int x, int y) { return at(y, x); },
        [&](int x, int y, float v) { work[at(y, x)] = v; });
    pass(height, width,
        [&](int y, int x) { return at(y, x); },
        [&](int y, int x, float v) { distance[at(y, x)] = std::sqrt(v); });
    return distance;
}

std::array<int, 4> bounds(std::vector<int> const& positions, int width) {
    std::array<int, 4> value{width, std::numeric_limits<int>::max(), -1, -1};
    for (int position : positions) {
        int const x = position % width;
        int const y = position / width;
        value[0] = std::min(value[0], x);
        value[1] = std::min(value[1], y);
        value[2] = std::max(value[2], x);
        value[3] = std::max(value[3], y);
    }
    return value;
}

Skeleton thin(std::vector<int> const& positions, int sourceWidth) {
    auto const box = bounds(positions, sourceWidth);
    Skeleton skeleton;
    skeleton.width = box[2] - box[0] + 3;
    skeleton.height = box[3] - box[1] + 3;
    skeleton.offsetX = box[0] - 1;
    skeleton.offsetY = box[1] - 1;
    skeleton.cells.assign(
        static_cast<std::size_t>(skeleton.width) * skeleton.height, 0);
    for (int position : positions) {
        int const x = position % sourceWidth - skeleton.offsetX;
        int const y = position / sourceWidth - skeleton.offsetY;
        skeleton.cells[static_cast<std::size_t>(y) * skeleton.width + x] = 1;
    }

    std::vector<int> remove;
    bool changed = false;
    do {
        changed = false;
        for (int phase = 0; phase < 2; ++phase) {
            remove.clear();
            for (int y = 1; y + 1 < skeleton.height; ++y) {
                for (int x = 1; x + 1 < skeleton.width; ++x) {
                    int const position = y * skeleton.width + x;
                    if (!skeleton.cells[static_cast<std::size_t>(position)]) continue;
                    std::array<int, 9> const neighbors{
                        skeleton.cells[static_cast<std::size_t>(position - skeleton.width)],
                        skeleton.cells[static_cast<std::size_t>(position - skeleton.width + 1)],
                        skeleton.cells[static_cast<std::size_t>(position + 1)],
                        skeleton.cells[static_cast<std::size_t>(position + skeleton.width + 1)],
                        skeleton.cells[static_cast<std::size_t>(position + skeleton.width)],
                        skeleton.cells[static_cast<std::size_t>(position + skeleton.width - 1)],
                        skeleton.cells[static_cast<std::size_t>(position - 1)],
                        skeleton.cells[static_cast<std::size_t>(position - skeleton.width - 1)],
                        skeleton.cells[static_cast<std::size_t>(position - skeleton.width)]
                    };
                    int adjacent = 0;
                    int transitions = 0;
                    for (std::size_t i = 0; i < 8; ++i) {
                        adjacent += neighbors[i];
                        if (!neighbors[i] && neighbors[i + 1]) ++transitions;
                    }
                    if (adjacent < 2 || adjacent > 6 || transitions != 1) continue;
                    bool const keep = phase == 0
                        ? neighbors[0] * neighbors[2] * neighbors[4] != 0 ||
                          neighbors[2] * neighbors[4] * neighbors[6] != 0
                        : neighbors[0] * neighbors[2] * neighbors[6] != 0 ||
                          neighbors[0] * neighbors[4] * neighbors[6] != 0;
                    if (!keep) remove.push_back(position);
                }
            }
            for (int position : remove) {
                skeleton.cells[static_cast<std::size_t>(position)] = 0;
            }
            changed = changed || !remove.empty();
        }
    } while (changed);
    return skeleton;
}

std::vector<std::vector<int>> skeletonPaths(Skeleton const& skeleton, bool pruneCornerShortcuts) {
    std::unordered_set<std::uint64_t> visited;
    std::vector<std::vector<int>> paths;

    auto walk = [&](int start, int next) {
        std::vector<int> path{start};
        int previous = start;
        int current = next;
        visited.insert(edgeKey(previous, current));
        while (true) {
            path.push_back(current);
            if (current == start && path.size() > 2) break;
            std::array<int, 8> neighbors{};
            int const count = skeletonNeighbors(skeleton, current, neighbors, pruneCornerShortcuts);
            if (current != start && count != 2) break;
            int following = -1;
            for (int i = 0; i < count; ++i) {
                if (neighbors[i] == previous || visited.contains(edgeKey(current, neighbors[i]))) {
                    continue;
                }
                following = neighbors[i];
                break;
            }
            if (following < 0) break;
            previous = current;
            current = following;
            visited.insert(edgeKey(previous, current));
        }
        if (path.size() >= 2) paths.push_back(std::move(path));
    };

    for (int position = 0; position < static_cast<int>(skeleton.cells.size()); ++position) {
        if (!skeleton.cells[static_cast<std::size_t>(position)]) continue;
        std::array<int, 8> neighbors{};
        int const count = skeletonNeighbors(skeleton, position, neighbors, pruneCornerShortcuts);
        if (count == 2) continue;
        for (int i = 0; i < count; ++i) {
            if (!visited.contains(edgeKey(position, neighbors[i]))) walk(position, neighbors[i]);
        }
    }
    for (int position = 0; position < static_cast<int>(skeleton.cells.size()); ++position) {
        if (!skeleton.cells[static_cast<std::size_t>(position)]) continue;
        std::array<int, 8> neighbors{};
        int const count = skeletonNeighbors(skeleton, position, neighbors, pruneCornerShortcuts);
        for (int i = 0; i < count; ++i) {
            if (!visited.contains(edgeKey(position, neighbors[i]))) walk(position, neighbors[i]);
        }
    }
    return paths;
}

std::vector<std::vector<int>> connectedComponents(
    std::vector<int> const& positions,
    int width,
    int height
) {
    std::vector<std::uint8_t> occupied(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) {
        if (position >= 0 && position < width * height) {
            occupied[static_cast<std::size_t>(position)] = 1;
        }
    }

    std::vector<std::vector<int>> components;
    std::queue<int> pending;
    for (int start : positions) {
        if (start < 0 || start >= width * height ||
            occupied[static_cast<std::size_t>(start)] != 1) {
            continue;
        }
        occupied[static_cast<std::size_t>(start)] = 2;
        pending.push(start);
        auto& component = components.emplace_back();
        while (!pending.empty()) {
            int const position = pending.front();
            pending.pop();
            component.push_back(position);
            int const x = position % width;
            int const y = position / width;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int const xx = x + dx;
                    int const yy = y + dy;
                    if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                    int const next = yy * width + xx;
                    if (occupied[static_cast<std::size_t>(next)] != 1) continue;
                    occupied[static_cast<std::size_t>(next)] = 2;
                    pending.push(next);
                }
            }
        }
    }
    return components;
}

} // namespace paimon::gifimport
