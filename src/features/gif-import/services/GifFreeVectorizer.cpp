#include "GifFreeVectorizer.hpp"

#include "GifPaintVectorizer.hpp"
#include "GifShapeRaster.hpp"
#include "GifStampCatalog.hpp"
#include "GifVectorMath.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace paimon::gifimport {

namespace {

// coarse signature: below this, cell-by-cell review doesn't pay.
constexpr float kStampFloor = 0.5f;
// near-full: paint does it better and merges neighbors.
constexpr float kRectCoverage = 0.93f;
// signature sorts but stays coarse: first alone never suffices.
constexpr int kExactTries = 6;
// must save 2: blind to plan rectangle merges.
constexpr std::size_t kStampSaving = 2;
constexpr int kSplitDepth = 5;
constexpr std::size_t kMinStampCells = 8;
constexpr int kSpillSamples = 3;

struct FreeContext {
    int width = 0;
    int height = 0;
    int color = 0;
    int rank = 0;
    int layer = 0;
    std::vector<std::uint8_t> remaining;
    std::vector<std::uint8_t> permitted;
    std::vector<std::uint8_t> const* blocked = nullptr;
    std::vector<std::uint8_t> const* empty = nullptr;
};

std::size_t paintCost(FreeContext const& context, std::vector<int> const& cells) {
    if (cells.empty()) return 0;
    return vectorizePaint(
        cells, context.width, context.height, context.color, context.rank,
        *context.blocked, *context.empty).size();
}

std::uint64_t boxSignature(
    std::vector<std::uint8_t> const& flags,
    int width,
    std::array<int, 4> const& box,
    bool wanted
) {
    int const boxWidth = box[2] - box[0] + 1;
    int const boxHeight = box[3] - box[1] + 1;
    std::uint64_t bits = 0;
    for (int row = 0; row < kStampSignatureSide; ++row) {
        int const y = box[1] + (row * 2 + 1) * boxHeight / (kStampSignatureSide * 2);
        for (int column = 0; column < kStampSignatureSide; ++column) {
            int const x = box[0] + (column * 2 + 1) * boxWidth / (kStampSignatureSide * 2);
            bool const set = flags[static_cast<std::size_t>(y) * width + x] != 0;
            if (set != wanted) continue;
            bits |= std::uint64_t{1} << (row * kStampSignatureSide + column);
        }
    }
    return bits;
}

ShapeXform stampXform(StampMask const& mask, std::array<int, 4> const& box) {
    ShapeXform shape;
    shape.width = static_cast<float>(box[2] - box[0] + 1);
    shape.height = static_cast<float>(box[3] - box[1] + 1);
    shape.x = static_cast<float>(box[0]) + shape.width * 0.5f;
    shape.y = static_cast<float>(box[1]) + shape.height * 0.5f;
    shape.extentX = shape.width * 0.5f;
    shape.extentY = shape.height * 0.5f;
    shape.kind = PrimitiveKind::Stamp;
    shape.mask = &mask;
    return shape;
}

struct Evaluation {
    int covered = 0;
    bool clean = true;
};

// signature misses half-cell tips: discard on spill.
Evaluation evaluate(FreeContext const& context, ShapeXform const& shape) {
    Evaluation result;
    auto const box = xformBox(shape, context.width, context.height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * context.width + x;
            if (context.permitted[index]) {
                if (context.remaining[index] &&
                    shape.contains(x + 0.5f, y + 0.5f)) {
                    ++result.covered;
                }
                continue;
            }
            for (int sampleY = 0; sampleY < kSpillSamples; ++sampleY) {
                for (int sampleX = 0; sampleX < kSpillSamples; ++sampleX) {
                    if (!shape.contains(
                            x + (sampleX + 0.5f) / kSpillSamples,
                            y + (sampleY + 0.5f) / kSpillSamples)) {
                        continue;
                    }
                    result.clean = false;
                    return result;
                }
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> coveredCells(FreeContext const& context, ShapeXform const& shape) {
    std::vector<std::uint8_t> shadow(context.remaining.size(), 0);
    auto const box = xformBox(shape, context.width, context.height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            if (!shape.contains(x + 0.5f, y + 0.5f)) continue;
            shadow[static_cast<std::size_t>(y) * context.width + x] = 1;
        }
    }
    return shadow;
}

void consume(FreeContext& context, ShapeXform const& shape) {
    auto const box = xformBox(shape, context.width, context.height);
    for (int y = box[1]; y <= box[3]; ++y) {
        for (int x = box[0]; x <= box[2]; ++x) {
            std::size_t const index = static_cast<std::size_t>(y) * context.width + x;
            if (!context.remaining[index]) continue;
            if (!shape.contains(x + 0.5f, y + 0.5f)) continue;
            context.remaining[index] = 0;
        }
    }
}

std::vector<int> stillMissing(FreeContext const& context, std::vector<int> const& cells) {
    std::vector<int> rest;
    for (int position : cells) {
        if (context.remaining[static_cast<std::size_t>(position)]) rest.push_back(position);
    }
    return rest;
}

void fitBlob(
    FreeContext& context,
    std::vector<int> const& cells,
    int depth,
    std::vector<Primitive>& output
) {
    if (cells.size() < kMinStampCells || depth > kSplitDepth) return;
    auto const box = bounds(cells, context.width);
    int const boxWidth = box[2] - box[0] + 1;
    int const boxHeight = box[3] - box[1] + 1;
    float const area = static_cast<float>(boxWidth) * boxHeight;
    if (static_cast<float>(cells.size()) >= area * kRectCoverage) return;

    std::size_t const plain = paintCost(context, cells);
    if (plain <= 1) return;

    auto const target = boxSignature(context.remaining, context.width, box, true);
    auto const forbidden = boxSignature(context.permitted, context.width, box, false);
    int const wanted = std::popcount(target);
    int const floor = static_cast<int>(std::ceil(wanted * kStampFloor));

    std::array<std::size_t, kExactTries> best{};
    std::array<int, kExactTries> scores{};
    int found = 0;
    auto const& variants = stampVariants();
    for (std::size_t index = 0; index < variants.size(); ++index) {
        auto const& variant = variants[index];
        if ((variant.signature & forbidden) != 0) continue;
        int const gain = std::popcount(variant.signature & target);
        if (gain < floor) continue;
        int const score = gain * 64 - (variant.filled - gain);
        int slot = found < kExactTries ? found : kExactTries - 1;
        if (found == kExactTries && score <= scores[slot]) continue;
        while (slot > 0 && scores[slot - 1] < score) {
            scores[slot] = scores[slot - 1];
            best[slot] = best[slot - 1];
            --slot;
        }
        scores[slot] = score;
        best[slot] = index;
        if (found < kExactTries) ++found;
    }

    for (int attempt = 0; attempt < found; ++attempt) {
        auto const slot = best[static_cast<std::size_t>(attempt)];
        auto const shape = stampXform(variants[slot].stamp.mask, box);
        auto const evaluation = evaluate(context, shape);
        if (!evaluation.clean || evaluation.covered <= 0) continue;

        // only in when mold + rest costs less than painting everything.
        auto const shadow = coveredCells(context, shape);
        std::vector<int> rest;
        rest.reserve(cells.size());
        for (int position : cells) {
            if (!shadow[static_cast<std::size_t>(position)]) rest.push_back(position);
        }
        if (1 + paintCost(context, rest) + kStampSaving > plain) continue;

        Primitive object;
        object.x = shape.x;
        object.y = shape.y;
        object.width = shape.width;
        object.height = shape.height;
        object.color = static_cast<std::uint16_t>(context.color);
        object.kind = PrimitiveKind::Stamp;
        object.layer = static_cast<std::int16_t>(context.layer);
        object.stamp = static_cast<std::uint16_t>(slot);
        output.push_back(object);

        consume(context, shape);
        for (auto const& piece :
             connectedComponents(rest, context.width, context.height)) {
            fitBlob(context, piece, depth + 1, output);
        }
        return;
    }

    // no paying mold: split along the long side and retry.
    std::vector<int> first;
    std::vector<int> second;
    if (boxWidth >= boxHeight) {
        int const cut = box[0] + boxWidth / 2;
        for (int position : cells) {
            (position % context.width < cut ? first : second).push_back(position);
        }
    } else {
        int const cut = box[1] + boxHeight / 2;
        for (int position : cells) {
            (position / context.width < cut ? first : second).push_back(position);
        }
    }
    if (first.empty() || second.empty()) return;
    for (auto const& half : {first, second}) {
        for (auto const& piece : connectedComponents(half, context.width, context.height)) {
            fitBlob(context, piece, depth + 1, output);
        }
    }
}

} // namespace

std::vector<Primitive> vectorizeFree(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked,
    std::vector<std::uint8_t> const& empty,
    bool gridExact
) {
    auto plain = vectorizePaint(positions, width, height, color, rank, blocked, empty, gridExact);
    if (positions.empty() || stampVariants().empty()) return plain;

    std::size_t const cells = static_cast<std::size_t>(width) * height;
    FreeContext context;
    context.width = width;
    context.height = height;
    context.color = color;
    context.rank = rank;
    context.layer = rank * kPaintSublayers;
    context.blocked = &blocked;
    context.empty = &empty;
    context.remaining.assign(cells, 0);
    for (int position : positions) {
        if (position >= 0 && static_cast<std::size_t>(position) < cells) {
            context.remaining[static_cast<std::size_t>(position)] = 1;
        }
    }
    context.permitted = context.remaining;
    if (blocked.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            context.permitted[position] |= blocked[position];
        }
    }
    if (empty.size() == cells) {
        for (std::size_t position = 0; position < cells; ++position) {
            context.permitted[position] |= empty[position];
        }
    }

    std::vector<Primitive> output;
    for (auto const& component : connectedComponents(positions, width, height)) {
        fitBlob(context, component, 0, output);
    }

    auto leftover = stillMissing(context, positions);
    if (!leftover.empty()) {
        auto rest = vectorizePaint(
            leftover, width, height, color, rank, blocked, empty, gridExact);
        output.insert(output.end(), rest.begin(), rest.end());
    }
    // free mode never costs more than paint.
    return output.size() < plain.size() ? output : plain;
}

} // namespace paimon::gifimport
