#include "GifCircleVectorizer.hpp"

#include "GifPaintVectorizer.hpp"
#include "GifShapeRaster.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace paimon::gifimport {

namespace {

// angles tried when stretching the ellipse. sixteen split the half-turn for
// diagonal strokes.
constexpr int kAngles = 16;
// waists tried: full circle down to a 0.22 spindle for thin lines.
constexpr std::array<float, 6> kWaists{1.f, 0.82f, 0.65f, 0.48f, 0.35f, 0.22f};
// axis step when measuring ellipse reach.
constexpr float kWalk = 0.5f;
// how far to walk.
constexpr float kReach = 64.f;
// minor radius of the smallest ellipse.
constexpr float kMinRadius = 0.5f;
// bleed given to the chosen ellipse, largest first.
constexpr std::array<float, 4> kBleeds{0.5f, 0.35f, 0.2f, 0.1f};
// how far an ellipse may spill over a visible color.
constexpr float kSpill = 0.14f;

struct Field {
    int width = 0;
    int height = 0;
    // per-cell clearance from its center before this color would show.
    std::vector<float> clearance;

    float at(float x, float y) const {
        int const cellX = static_cast<int>(std::floor(x));
        int const cellY = static_cast<int>(std::floor(y));
        if (cellX < 0 || cellY < 0 || cellX >= width || cellY >= height) return 0.f;
        return clearance[static_cast<std::size_t>(cellY) * width + cellX];
    }
};

Primitive ellipse(
    Point const& center,
    float major,
    float minor,
    float angle,
    int color,
    int layer
) {
    return {
        center.x,
        center.y,
        major * 2.f,
        minor * 2.f,
        angle * 180.f / kPi,
        static_cast<std::uint16_t>(color),
        PrimitiveKind::Circle,
        static_cast<std::int16_t>(layer)
    };
}

// missing cells the ellipse takes plus already-painted ones it repeats.
// repeats don't hurt (same color), but ties break toward less overlap.
struct Gain {
    int fresh = 0;
    int repeated = 0;
};

Gain measure(
    Primitive const& shape,
    std::vector<std::uint8_t> const& remaining,
    std::vector<std::uint8_t> const& target,
    int width,
    int height
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    Gain gain;
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if (!target[index] || !placed.contains(x + 0.5f, y + 0.5f)) continue;
            if (remaining[index]) {
                ++gain.fresh;
            } else {
                ++gain.repeated;
            }
        }
    }
    return gain;
}

void consume(
    Primitive const& shape,
    std::vector<std::uint8_t>& remaining,
    int width,
    int height
) {
    auto const placed = xformOf(shape);
    auto const box = xformBox(placed, width, height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * width + x;
            if (remaining[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                remaining[index] = 0;
            }
        }
    }
}

// the ellipse fits whole when the fattest circle fits the whole run:
// an ellipse is the union of the circles resting on its axis.
Primitive stretch(
    Field const& field,
    Point const& seed,
    float angle,
    float minor,
    int color,
    int layer
) {
    float const dirX = std::cos(angle);
    float const dirY = std::sin(angle);
    std::array<float, 2> reach{0.f, 0.f};
    for (int side = 0; side < 2; ++side) {
        float const sign = side ? 1.f : -1.f;
        float walked = 0.f;
        while (walked + kWalk <= kReach &&
               field.at(seed.x + dirX * sign * (walked + kWalk),
                        seed.y + dirY * sign * (walked + kWalk)) >= minor) {
            walked += kWalk;
        }
        reach[static_cast<std::size_t>(side)] = walked;
    }
    float const shift = (reach[1] - reach[0]) * 0.5f;
    float const major = std::max((reach[0] + reach[1]) * 0.5f + minor, minor);
    return ellipse(
        {seed.x + dirX * shift, seed.y + dirY * shift},
        major, minor, angle, color, layer);
}

void pruneCircles(
    std::vector<Primitive>& objects,
    std::vector<std::uint8_t> const& target,
    int width,
    int height
) {
    if (objects.size() <= 1) return;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    std::vector<int> count(cells, 0);

    for (auto const& object : objects) {
        auto const placed = xformOf(object);
        auto const box = xformBox(placed, width, height);
        for (int y = box[1]; y <= box[3]; ++y) {
            for (int x = box[0]; x <= box[2]; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * width + x;
                if (target[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                    ++count[index];
                }
            }
        }
    }

    std::vector<std::size_t> order(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return (objects[left].width * objects[left].height) <
               (objects[right].width * objects[right].height);
    });

    std::vector<std::uint8_t> removed(objects.size(), 0);
    for (std::size_t idx : order) {
        auto const placed = xformOf(objects[idx]);
        auto const box = xformBox(placed, width, height);
        bool canDrop = true;
        for (int y = box[1]; y <= box[3]; ++y) {
            for (int x = box[0]; x <= box[2]; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * width + x;
                if (target[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                    if (count[index] <= 1) {
                        canDrop = false;
                        break;
                    }
                }
            }
            if (!canDrop) break;
        }
        if (canDrop) {
            removed[idx] = 1;
            for (int y = box[1]; y <= box[3]; ++y) {
                for (int x = box[0]; x <= box[2]; ++x) {
                    std::size_t const index = static_cast<std::size_t>(y) * width + x;
                    if (target[index] && placed.contains(x + 0.5f, y + 0.5f)) {
                        --count[index];
                    }
                }
            }
        }
    }

    std::vector<Primitive> kept;
    kept.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (!removed[i]) kept.push_back(objects[i]);
    }
    objects = std::move(kept);
}

} // namespace

std::vector<Primitive> vectorizeCircles(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& empty
) {
    std::vector<Primitive> output;
    if (positions.empty()) return output;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    int const layer = rank * kPaintSublayers;

    std::vector<std::uint8_t> target(cells, 0);
    for (int position : positions) {
        if (position >= 0 && static_cast<std::size_t>(position) < cells) {
            target[static_cast<std::size_t>(position)] = 1;
        }
    }
    // growth may cover same-color cells or cells hidden by later layers; void would enlarge the silhouette.
    std::vector<std::uint8_t> room = target;
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            room[position] |= blocked[position];
        }
    }
    std::vector<std::uint8_t> permitted = room;
    if (empty.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            permitted[position] |= empty[position];
        }
    }

    Field field;
    field.width = width;
    field.height = height;
    field.clearance = distanceField(room, width, height);
    // distance runs center to center; the neighbor cell starts half early,
    // so without this the ellipse always overshoots that half cell.
    for (auto& value : field.clearance) value = std::max(value - 0.5f, 0.f);

    std::vector<std::uint8_t> remaining = target;
    for (auto const& component : connectedComponents(positions, width, height)) {
        if (component.size() >= 4) {
            auto const box = bounds(component, width);
            float const boxW = static_cast<float>(box[2] - box[0] + 1);
            float const boxH = static_cast<float>(box[3] - box[1] + 1);
            float const aspect = std::max(boxW, boxH) / std::min(boxW, boxH);
            if (boxW >= 3.f && boxH >= 3.f && aspect <= 1.6f) {
                Point const center{
                    (box[0] + box[2] + 1) * 0.5f,
                    (box[1] + box[3] + 1) * 0.5f
                };
                Primitive const wholeCircle = ellipse(
                    center, boxW * 0.5f, boxH * 0.5f, 0.f, color, layer);
                Gain const gain = measure(wholeCircle, remaining, target, width, height);
                if (gain.fresh >= static_cast<int>(component.size() * 0.90f) &&
                    shapeSpill(wholeCircle, permitted, width, height) <= kSpill) {
                    consume(wholeCircle, remaining, width, height);
                    output.push_back(wholeCircle);
                    bool allDone = true;
                    for (int pos : component) {
                        if (remaining[static_cast<std::size_t>(pos)]) {
                            allDone = false;
                            break;
                        }
                    }
                    if (allDone) continue;
                }
            }
        }
        std::vector<int> pending = component;
        while (!pending.empty()) {
            // always seed where the blob is fattest: biggest ellipse first, the
            // rest split its leftovers, painting by blob instead of cell by cell.
            int seedCell = -1;
            float widest = -1.f;
            std::size_t alive = 0;
            for (int position : pending) {
                if (!remaining[static_cast<std::size_t>(position)]) continue;
                pending[alive++] = position;
                float const space = field.clearance[static_cast<std::size_t>(position)];
                if (space > widest) {
                    widest = space;
                    seedCell = position;
                }
            }
            pending.resize(alive);
            if (seedCell < 0) break;

            Point const seed{
                static_cast<float>(seedCell % width) + 0.5f,
                static_cast<float>(seedCell / width) + 0.5f
            };
            Primitive best;
            Gain bestGain;
            for (int step = 0; step < kAngles; ++step) {
                float const angle = step * kPi / kAngles;
                for (float waist : kWaists) {
                    float const minor = std::max(widest * waist, kMinRadius);
                    auto const candidate = stretch(
                        field, seed, angle, minor, color, layer);
                    auto const gain = measure(
                        candidate, remaining, target, width, height);
                    if (gain.fresh < bestGain.fresh ||
                        (gain.fresh == bestGain.fresh &&
                         gain.repeated >= bestGain.repeated)) {
                        continue;
                    }
                    if (shapeSpill(candidate, permitted, width, height) > kSpill) continue;
                    best = candidate;
                    bestGain = gain;
                }
            }
            if (bestGain.fresh <= 0) {
                // an unreachable cell needs its own ellipse to guarantee progress.
                best = ellipse(seed, kMinRadius, kMinRadius, 0.f, color, layer);
                remaining[static_cast<std::size_t>(seedCell)] = 0;
            }
            // just enough bleed for neighbors to touch (continuous stroke),
            // stopping where another color starts.
            for (float bleed : kBleeds) {
                Primitive fatter = best;
                fatter.width += bleed * 2.f;
                fatter.height += bleed * 2.f;
                if (shapeSpill(fatter, permitted, width, height) > kSpill) continue;
                best = fatter;
                break;
            }
            consume(best, remaining, width, height);
            output.push_back(best);
        }
    }
    pruneCircles(output, target, width, height);
    return output;
}

} // namespace paimon::gifimport
