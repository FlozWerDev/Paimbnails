#include "GifArtVectorizer.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace paimon::gifimport {

namespace {

bool contains(Primitive const& object, float x, float y) {
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

Primitive fitStroke(std::vector<int> const& positions, int width, int color) {
    float meanX = 0.f;
    float meanY = 0.f;
    for (int position : positions) {
        meanX += static_cast<float>(position % width) + 0.5f;
        meanY += static_cast<float>(position / width) + 0.5f;
    }
    float const count = static_cast<float>(positions.size());
    meanX /= count;
    meanY /= count;

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
        float const x = static_cast<float>(position % width) + 0.5f - meanX;
        float const y = static_cast<float>(position / width) + 0.5f - meanY;
        float const major = x * cosine + y * sine;
        float const minor = -x * sine + y * cosine;
        minMajor = std::min(minMajor, major);
        maxMajor = std::max(maxMajor, major);
        minMinor = std::min(minMinor, minor);
        maxMinor = std::max(maxMinor, minor);
    }

    float const padding = 0.5f * (std::abs(cosine) + std::abs(sine));
    float const middleMajor = (minMajor + maxMajor) * 0.5f;
    float const middleMinor = (minMinor + maxMinor) * 0.5f;
    float const objectWidth = maxMajor - minMajor + padding * 2.f;
    float const measuredHeight = maxMinor - minMinor + padding * 2.f;
    float const areaHeight = count / std::max(objectWidth, 0.01f);
    return {
        meanX + middleMajor * cosine - middleMinor * sine,
        meanY + middleMajor * sine + middleMinor * cosine,
        objectWidth,
        std::min(measuredHeight, std::max(areaHeight, 0.7f)),
        angle * 180.f / kPi,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Stroke
    };
}

std::vector<int> uncovered(
    std::vector<int> const& positions,
    int width,
    std::vector<Primitive> const& objects
) {
    std::vector<int> result;
    for (int position : positions) {
        float const x = static_cast<float>(position % width) + 0.5f;
        float const y = static_cast<float>(position / width) + 0.5f;
        bool covered = false;
        for (auto const& object : objects) {
            if (contains(object, x, y)) {
                covered = true;
                break;
            }
        }
        if (!covered) result.push_back(position);
    }
    return result;
}

struct ExtraCoverage {
    int total = 0;
    int blocked = 0;
};

ExtraCoverage extraCoverage(
    std::vector<int> const& positions,
    int width,
    int height,
    std::vector<Primitive> const& objects,
    std::vector<std::uint8_t> const& blocked
) {
    std::vector<std::uint8_t> target(static_cast<std::size_t>(width) * height, 0);
    for (int position : positions) target[static_cast<std::size_t>(position)] = 1;
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (auto const& object : objects) {
        float const angle = object.rotation * kPi / 180.f;
        float const extentX = std::abs(std::cos(angle)) * object.width * 0.5f +
            std::abs(std::sin(angle)) * object.height * 0.5f;
        float const extentY = std::abs(std::sin(angle)) * object.width * 0.5f +
            std::abs(std::cos(angle)) * object.height * 0.5f;
        minX = std::min(minX, std::max(0, static_cast<int>(std::floor(object.x - extentX))));
        minY = std::min(minY, std::max(0, static_cast<int>(std::floor(object.y - extentY))));
        maxX = std::max(maxX, std::min(width - 1, static_cast<int>(std::ceil(object.x + extentX))));
        maxY = std::max(maxY, std::min(height - 1, static_cast<int>(std::ceil(object.y + extentY))));
    }
    ExtraCoverage coverage;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            int const position = y * width + x;
            if (target[static_cast<std::size_t>(position)]) continue;
            bool centerCovered = false;
            for (auto const& object : objects) {
                if (contains(object, x + 0.5f, y + 0.5f)) {
                    centerCovered = true;
                    break;
                }
            }
            if (centerCovered) ++coverage.total;
            if (blocked.size() != target.size() ||
                !blocked[static_cast<std::size_t>(position)]) {
                continue;
            }
            bool touchesBlocked = false;
            for (int sampleY = 0; sampleY < 4 && !touchesBlocked; ++sampleY) {
                for (int sampleX = 0; sampleX < 4 && !touchesBlocked; ++sampleX) {
                    float const xx = x + (sampleX + 0.5f) / 4.f;
                    float const yy = y + (sampleY + 0.5f) / 4.f;
                    touchesBlocked = std::any_of(
                        objects.begin(), objects.end(), [&](Primitive const& object) {
                            return contains(object, xx, yy);
                        });
                }
            }
            if (touchesBlocked) ++coverage.blocked;
        }
    }
    return coverage;
}

void consider(
    std::vector<Primitive> objects,
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked,
    std::vector<Primitive>& best,
    float& bestScore
) {
    auto missing = uncovered(positions, width, objects);
    auto residual = packBlocks(missing, width, height, color);
    objects.insert(objects.end(), residual.begin(), residual.end());
    auto const extra = extraCoverage(positions, width, height, objects, blocked);
    bool const hasStroke = std::any_of(objects.begin(), objects.end(), [](Primitive const& object) {
        return object.kind == PrimitiveKind::Stroke;
    });
    int const divisor = hasStroke ? 3 : 12;
    int const extraLimit = std::max(1, static_cast<int>(positions.size() / divisor));
    if (extra.blocked > 0 || extra.total > extraLimit) return;
    float const score = static_cast<float>(objects.size()) +
        extra.total * (hasStroke ? 0.2f : 0.45f);
    if (score + 0.001f >= bestScore) return;
    bestScore = score;
    best = std::move(objects);
}

std::vector<Primitive> segmentedStrokes(
    std::vector<int> positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked,
    int depth
) {
    auto best = packBlocks(positions, width, height, color);
    float bestScore = static_cast<float>(best.size());
    if (positions.size() < 3) return best;

    auto stroke = fitStroke(positions, width, color);
    float const aspect = std::max(stroke.width, stroke.height) /
        std::max(std::min(stroke.width, stroke.height), 0.01f);
    if (aspect >= 1.45f) {
        consider({stroke}, positions, width, height, color, blocked, best, bestScore);
    }
    if (depth >= 4 || positions.size() < 10) return best;

    float const angle = stroke.rotation * kPi / 180.f;
    float const cosine = std::cos(angle);
    float const sine = std::sin(angle);
    std::sort(positions.begin(), positions.end(), [&](int left, int right) {
        auto projection = [&](int position) {
            return (static_cast<float>(position % width) + 0.5f) * cosine +
                (static_cast<float>(position / width) + 0.5f) * sine;
        };
        return projection(left) < projection(right);
    });
    auto middle = positions.begin() + static_cast<std::ptrdiff_t>(positions.size() / 2);
    std::vector<int> left(positions.begin(), middle);
    std::vector<int> right(middle, positions.end());
    auto split = segmentedStrokes(std::move(left), width, height, color, blocked, depth + 1);
    auto tail = segmentedStrokes(std::move(right), width, height, color, blocked, depth + 1);
    split.insert(split.end(), tail.begin(), tail.end());
    consider(std::move(split), positions, width, height, color, blocked, best, bestScore);
    return best;
}

std::vector<Primitive> skeletonStrokes(
    std::vector<int> const& component,
    int sourceWidth,
    int color
) {
    auto skeleton = thin(component, sourceWidth);
    auto paths = skeletonPaths(skeleton);
    float pathLength = 0.f;
    std::vector<std::vector<Point>> points;
    points.reserve(paths.size());
    for (auto const& path : paths) {
        auto& converted = points.emplace_back();
        converted.reserve(path.size());
        for (int position : path) {
            converted.push_back({
                static_cast<float>(position % skeleton.width + skeleton.offsetX) + 0.5f,
                static_cast<float>(position / skeleton.width + skeleton.offsetY) + 0.5f
            });
        }
        for (std::size_t i = 1; i < converted.size(); ++i) {
            pathLength += pointDistance(converted[i - 1], converted[i]);
        }
    }
    if (pathLength <= 0.01f) return {};

    float const thickness = std::clamp(
        static_cast<float>(component.size()) / pathLength, 0.7f, 12.f);
    float const tolerance = std::clamp(thickness * 0.3f, 0.45f, 1.1f);
    std::vector<Primitive> objects;
    auto appendPath = [&](std::vector<Point> const& path) {
        auto reduced = simplify(path, tolerance);
        for (std::size_t i = 1; i < reduced.size(); ++i) {
            auto const& first = reduced[i - 1];
            auto const& last = reduced[i];
            float const length = pointDistance(first, last);
            if (length <= 0.1f) continue;
            objects.push_back({
                (first.x + last.x) * 0.5f,
                (first.y + last.y) * 0.5f,
                length + thickness * 0.55f,
                thickness * 1.05f,
                std::atan2(last.y - first.y, last.x - first.x) * 180.f / kPi,
                static_cast<std::uint16_t>(color),
                PrimitiveKind::Stroke
            });
        }
    };

    for (auto const& path : points) {
        if (path.size() < 2) continue;
        if (path.size() > 3 && pointDistance(path.front(), path.back()) < 0.01f) {
            std::size_t split = 1;
            float farthest = 0.f;
            for (std::size_t i = 1; i + 1 < path.size(); ++i) {
                float const distance = pointDistance(path.front(), path[i]);
                if (distance > farthest) {
                    farthest = distance;
                    split = i;
                }
            }
            std::vector<Point> first(
                path.begin(), path.begin() + static_cast<std::ptrdiff_t>(split + 1));
            std::vector<Point> second(
                path.begin() + static_cast<std::ptrdiff_t>(split), path.end());
            appendPath(first);
            appendPath(second);
        } else {
            appendPath(path);
        }
    }
    return objects;
}

} // namespace

std::vector<Primitive> packBlocks(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& spare
) {
    if (positions.empty() || width <= 0 || height <= 0) return {};

    auto const box = bounds(positions, width);
    int const localWidth = box[2] - box[0] + 1;
    int const localHeight = box[3] - box[1] + 1;
    std::vector<std::uint8_t> source(
        static_cast<std::size_t>(localWidth) * localHeight, 0);
    for (int position : positions) {
        if (position < 0 || position >= width * height) continue;
        int const x = position % width - box[0];
        int const y = position / width - box[1];
        source[static_cast<std::size_t>(y) * localWidth + x] = 1;
    }
    // 1 es celda que hay que cubrir, 2 es celda que se puede pisar de paso.
    bool const hasSpare = spare.size() == static_cast<std::size_t>(width) * height;
    if (hasSpare) {
        for (int y = 0; y < localHeight; ++y) {
            for (int x = 0; x < localWidth; ++x) {
                auto& cell = source[static_cast<std::size_t>(y) * localWidth + x];
                if (cell) continue;
                auto const global =
                    static_cast<std::size_t>(y + box[1]) * width + x + box[0];
                if (spare[global]) cell = 2;
            }
        }
    }

    struct Rect {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };
    // Se busca el rectangulo que tape mas celdas pendientes, no el de mas area:
    // atravesar celdas de paso solo compensa si de camino se lleva trabajo por
    // delante. A igual cantidad gana el mas grande, que deja menos costuras.
    auto sweep = [](std::vector<std::uint8_t> cells, int gridWidth, int gridHeight) {
        std::vector<int> rowSum(
            static_cast<std::size_t>(gridHeight) * (gridWidth + 1), 0);
        auto rebuildRow = [&](int y) {
            auto* row = &rowSum[static_cast<std::size_t>(y) * (gridWidth + 1)];
            for (int x = 0; x < gridWidth; ++x) {
                row[x + 1] = row[x] +
                    (cells[static_cast<std::size_t>(y) * gridWidth + x] == 1 ? 1 : 0);
            }
        };
        for (int y = 0; y < gridHeight; ++y) rebuildRow(y);
        auto pending = [&](int y, int from, int to) {
            auto const* row = &rowSum[static_cast<std::size_t>(y) * (gridWidth + 1)];
            return row[to] - row[from];
        };

        std::vector<Rect> rectangles;
        for (int y = 0; y < gridHeight; ++y) {
            for (int x = 0; x < gridWidth; ++x) {
                if (cells[static_cast<std::size_t>(y) * gridWidth + x] != 1) continue;
                int maxWidth = 0;
                while (x + maxWidth < gridWidth &&
                       cells[static_cast<std::size_t>(y) * gridWidth + x + maxWidth]) {
                    ++maxWidth;
                }
                int bestWidth = maxWidth;
                int bestHeight = 1;
                int bestArea = maxWidth;
                int bestCovered = pending(y, x, x + maxWidth);
                int runningWidth = maxWidth;
                int covered = bestCovered;
                for (int yy = y + 1; yy < gridHeight; ++yy) {
                    int rowWidth = 0;
                    while (rowWidth < runningWidth &&
                           cells[static_cast<std::size_t>(yy) * gridWidth + x + rowWidth]) {
                        ++rowWidth;
                    }
                    if (rowWidth == 0) break;
                    if (rowWidth < runningWidth) {
                        runningWidth = rowWidth;
                        covered = 0;
                        for (int row = y; row < yy; ++row) {
                            covered += pending(row, x, x + runningWidth);
                        }
                    }
                    covered += pending(yy, x, x + runningWidth);
                    int const area = runningWidth * (yy - y + 1);
                    if (covered > bestCovered ||
                        (covered == bestCovered && area > bestArea)) {
                        bestCovered = covered;
                        bestArea = area;
                        bestWidth = runningWidth;
                        bestHeight = yy - y + 1;
                    }
                }
                for (int yy = y; yy < y + bestHeight; ++yy) {
                    for (int xx = x; xx < x + bestWidth; ++xx) {
                        auto& cell = cells[static_cast<std::size_t>(yy) * gridWidth + xx];
                        if (cell == 1) cell = 2;
                    }
                    rebuildRow(yy);
                }
                rectangles.push_back({x, y, bestWidth, bestHeight});
            }
        }
        return rectangles;
    };

    std::vector<Rect> best;
    // The greedy sweep is directional, so try every mirrored and transposed view.
    for (int transform = 0; transform < 8; ++transform) {
        bool const transpose = transform >= 4;
        bool const flipX = (transform & 1) != 0;
        bool const flipY = (transform & 2) != 0;
        int const transformedWidth = transpose ? localHeight : localWidth;
        int const transformedHeight = transpose ? localWidth : localHeight;
        std::vector<std::uint8_t> cells(
            static_cast<std::size_t>(transformedWidth) * transformedHeight, 0);
        for (int y = 0; y < localHeight; ++y) {
            for (int x = 0; x < localWidth; ++x) {
                auto const value = source[static_cast<std::size_t>(y) * localWidth + x];
                if (!value) continue;
                int xx = transpose ? y : x;
                int yy = transpose ? x : y;
                if (flipX) xx = transformedWidth - 1 - xx;
                if (flipY) yy = transformedHeight - 1 - yy;
                cells[static_cast<std::size_t>(yy) * transformedWidth + xx] = value;
            }
        }

        auto transformed = sweep(
            std::move(cells), transformedWidth, transformedHeight);
        std::vector<Rect> candidate;
        candidate.reserve(transformed.size());
        for (auto const& rectangle : transformed) {
            int minX = localWidth;
            int minY = localHeight;
            int maxX = -1;
            int maxY = -1;
            for (auto const [tx, ty] : std::array<std::pair<int, int>, 4>{
                     std::pair{rectangle.x, rectangle.y},
                     std::pair{rectangle.x + rectangle.width - 1, rectangle.y},
                     std::pair{rectangle.x, rectangle.y + rectangle.height - 1},
                     std::pair{rectangle.x + rectangle.width - 1,
                               rectangle.y + rectangle.height - 1}}) {
                int xx = flipX ? transformedWidth - 1 - tx : tx;
                int yy = flipY ? transformedHeight - 1 - ty : ty;
                int const x = transpose ? yy : xx;
                int const y = transpose ? xx : yy;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
            int const rectangleWidth = maxX - minX + 1;
            int const rectangleHeight = maxY - minY + 1;
            candidate.push_back({minX, minY, rectangleWidth, rectangleHeight});
        }
        if (best.empty() || candidate.size() < best.size()) {
            best = std::move(candidate);
        }
        if (best.size() == 1) break;
    }

    std::vector<Primitive> objects;
    objects.reserve(best.size());
    for (auto const& rectangle : best) {
        objects.push_back({
            box[0] + rectangle.x + rectangle.width * 0.5f,
            box[1] + rectangle.y + rectangle.height * 0.5f,
            static_cast<float>(rectangle.width),
            static_cast<float>(rectangle.height),
            0.f,
            static_cast<std::uint16_t>(color),
            PrimitiveKind::Block
        });
    }
    return objects;
}

std::vector<Primitive> vectorizeArt(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    std::vector<std::uint8_t> const& blocked
) {
    std::vector<Primitive> output;
    for (auto const& component : connectedComponents(positions, width, height)) {
        auto best = packBlocks(component, width, height, color);
        float bestScore = static_cast<float>(best.size());
        auto const box = bounds(component, width);
        float const boxWidth = static_cast<float>(box[2] - box[0] + 1);
        float const boxHeight = static_cast<float>(box[3] - box[1] + 1);
        float const centerX = (box[0] + box[2] + 1) * 0.5f;
        float const centerY = (box[1] + box[3] + 1) * 0.5f;

        if (boxWidth >= 3.f && boxHeight >= 3.f) {
            consider({{
                centerX, centerY, boxWidth, boxHeight, 0.f,
                static_cast<std::uint16_t>(color), PrimitiveKind::Circle
            }}, component, width, height, color, blocked, best, bestScore);

            for (int quarter = 0; quarter < 4; ++quarter) {
                bool const sideways = quarter % 2 != 0;
                float const localWidth = sideways ? boxHeight : boxWidth;
                float const localHeight = sideways ? boxWidth : boxHeight;
                PrimitiveKind const kind = localWidth / std::max(localHeight, 0.01f) >= 1.5f
                    ? PrimitiveKind::WideTriangle
                    : PrimitiveKind::Triangle;
                consider({{
                    centerX, centerY, localWidth, localHeight, quarter * 90.f,
                    static_cast<std::uint16_t>(color), kind
                }}, component, width, height, color, blocked, best, bestScore);
            }
        }

        if (component.size() >= 3) {
            auto strokes = segmentedStrokes(component, width, height, color, blocked, 0);
            consider(
                std::move(strokes), component, width, height, color, blocked, best, bestScore);
        }
        if (component.size() >= 8) {
            auto strokes = skeletonStrokes(component, width, color);
            if (!strokes.empty()) {
                consider(
                    std::move(strokes), component, width, height, color, blocked,
                    best, bestScore);
            }
        }
        output.insert(output.end(), best.begin(), best.end());
    }
    return output;
}

std::vector<std::uint8_t> renderPlanFrame(ImportPlan const& plan, int frame, int scale) {
    scale = std::clamp(scale, 1, 8);
    int const outputWidth = plan.width * scale;
    int const outputHeight = plan.height * scale;
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(outputWidth) * outputHeight * 4, 0);
    frame = std::clamp(frame, 0, std::max(static_cast<int>(plan.frames.size()) - 1, 0));

    auto draw = [&](Primitive const& object) {
        if (object.color >= plan.palette.size()) return;
        auto const& color = plan.palette[object.color];
        float const angle = object.rotation * kPi / 180.f;
        float const extentX = std::abs(std::cos(angle)) * object.width * 0.5f +
            std::abs(std::sin(angle)) * object.height * 0.5f;
        float const extentY = std::abs(std::sin(angle)) * object.width * 0.5f +
            std::abs(std::cos(angle)) * object.height * 0.5f;
        int const minX = std::clamp(
            static_cast<int>(std::floor((object.x - extentX) * scale)), 0, outputWidth - 1);
        int const minY = std::clamp(
            static_cast<int>(std::floor((object.y - extentY) * scale)), 0, outputHeight - 1);
        int const maxX = std::clamp(
            static_cast<int>(std::ceil((object.x + extentX) * scale)), 0, outputWidth - 1);
        int const maxY = std::clamp(
            static_cast<int>(std::ceil((object.y + extentY) * scale)), 0, outputHeight - 1);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                float const sampleX = (x + 0.5f) / scale;
                float const sampleY = (y + 0.5f) / scale;
                if (!contains(object, sampleX, sampleY)) continue;
                std::size_t const index = (static_cast<std::size_t>(y) * outputWidth + x) * 4;
                pixels[index] = color.r;
                pixels[index + 1] = color.g;
                pixels[index + 2] = color.b;
                pixels[index + 3] = 255;
            }
        }
    };

    std::vector<Primitive const*> visible;
    visible.reserve(plan.staticObjects.size());
    for (auto const& object : plan.staticObjects) visible.push_back(&object);
    for (auto const& track : plan.tracks) {
        if (track.mask.empty() ||
            (track.mask[static_cast<std::size_t>(frame / 64)] &
             (std::uint64_t{1} << (frame % 64))) == 0) {
            continue;
        }
        for (auto const& object : track.objects) visible.push_back(&object);
    }
    std::stable_sort(visible.begin(), visible.end(), [](auto* left, auto* right) {
        return left->layer < right->layer;
    });
    for (auto const* object : visible) draw(*object);
    return pixels;
}

} // namespace paimon::gifimport
