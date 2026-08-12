#include "GifImportPipeline.hpp"
#include "GifArtVectorizer.hpp"
#include "GifPaintVectorizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <utility>

namespace paimon::gifimport {

namespace {

constexpr std::size_t kPlaybackTriggerLimit = 512;
constexpr std::size_t kTriggerRuntimeWeight = 8;

struct Pixel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;
};

struct ReducedFrame {
    int delayMs = 100;
    std::vector<Pixel> pixels;
};

struct SelectedFrame {
    SourceFrame const* frame = nullptr;
    int delayMs = 100;
};

struct Candidate {
    std::vector<Primitive> staticObjects;
    std::vector<VisibilityTrack> tracks;
    std::size_t triggers = 0;
    std::string strategy;

    std::size_t visuals() const {
        std::size_t count = staticObjects.size();
        for (auto const& track : tracks) count += track.objects.size();
        return count;
    }

    std::size_t total() const { return visuals() + triggers; }

    std::size_t runtimeCost() const {
        return visuals() + triggers * kTriggerRuntimeWeight;
    }
};

struct BucketKey {
    int color = 0;
    std::vector<std::uint64_t> mask;

    bool operator<(BucketKey const& other) const {
        if (color != other.color) return color < other.color;
        return mask < other.mask;
    }
};

Options sanitize(Options options) {
    options.maxDimension = std::clamp(options.maxDimension, 4, 160);
    options.minDimension = std::clamp(options.minDimension, 4, options.maxDimension);
    options.maxColors = std::clamp(options.maxColors, 1, 64);
    options.maxFrames = std::clamp(options.maxFrames, 1, 120);
    options.objectBudget = std::clamp(options.objectBudget, 100, 50000);
    options.alphaThreshold = std::clamp(options.alphaThreshold, 1, 254);
    options.backgroundTolerance = std::clamp(options.backgroundTolerance, 0, 120);
    options.pixelSize = std::clamp(options.pixelSize, 1.f, 30.f);
    if (options.mode != ImportMode::Blocks) {
        options.sampling = SamplingMode::Smooth;
        options.dither = false;
    }
    return options;
}

std::vector<SelectedFrame> selectFrames(SourceAnimation const& source, int limit) {
    int const count = static_cast<int>(source.frames.size());
    if (count <= limit) {
        std::vector<SelectedFrame> selected;
        selected.reserve(source.frames.size());
        for (auto const& frame : source.frames) {
            selected.push_back({&frame, std::max(frame.delayMs, 10)});
        }
        return selected;
    }

    std::vector<SelectedFrame> selected;
    selected.reserve(static_cast<std::size_t>(limit));
    for (int i = 0; i < limit; ++i) {
        int const begin = i * count / limit;
        int const end = (i + 1) * count / limit;
        int const chosen = begin + (end - begin - 1) / 2;
        long long delay = 0;
        for (int j = begin; j < end; ++j) {
            delay += std::max(source.frames[static_cast<std::size_t>(j)].delayMs, 10);
        }
        selected.push_back({
            &source.frames[static_cast<std::size_t>(chosen)],
            static_cast<int>(std::min<long long>(delay, std::numeric_limits<int>::max()))
        });
    }
    return selected;
}

Pixel sourcePixel(SourceFrame const& frame, std::size_t index) {
    std::size_t const p = index * 4;
    return {frame.rgba[p], frame.rgba[p + 1], frame.rgba[p + 2], frame.rgba[p + 3]};
}

int colorDistanceSq(Pixel const& a, Pixel const& b) {
    int const dr = static_cast<int>(a.r) - b.r;
    int const dg = static_cast<int>(a.g) - b.g;
    int const db = static_cast<int>(a.b) - b.b;
    return dr * dr + dg * dg + db * db;
}

std::vector<std::uint8_t> backgroundMask(
    SourceFrame const& frame,
    int width,
    int height,
    Options const& options
) {
    std::vector<std::uint8_t> removed(static_cast<std::size_t>(width) * height, 0);
    if (options.background != BackgroundMode::AutoBorder) return removed;

    struct Bin {
        std::uint32_t count = 0;
        std::uint64_t r = 0;
        std::uint64_t g = 0;
        std::uint64_t b = 0;
    };
    std::array<Bin, 4096> bins{};

    auto addBorder = [&](int x, int y) {
        auto const pixel = sourcePixel(frame, static_cast<std::size_t>(y) * width + x);
        if (pixel.a < options.alphaThreshold) return;
        int const key = (pixel.r >> 4) << 8 | (pixel.g >> 4) << 4 | (pixel.b >> 4);
        auto& bin = bins[static_cast<std::size_t>(key)];
        ++bin.count;
        bin.r += pixel.r;
        bin.g += pixel.g;
        bin.b += pixel.b;
    };
    for (int x = 0; x < width; ++x) {
        addBorder(x, 0);
        if (height > 1) addBorder(x, height - 1);
    }
    for (int y = 1; y + 1 < height; ++y) {
        addBorder(0, y);
        if (width > 1) addBorder(width - 1, y);
    }

    auto best = std::max_element(bins.begin(), bins.end(), [](Bin const& a, Bin const& b) {
        return a.count < b.count;
    });
    if (best == bins.end() || best->count == 0) return removed;

    Pixel background{
        static_cast<std::uint8_t>(best->r / best->count),
        static_cast<std::uint8_t>(best->g / best->count),
        static_cast<std::uint8_t>(best->b / best->count),
        255
    };
    int const maxDistance = options.backgroundTolerance * options.backgroundTolerance * 3;
    std::vector<std::uint8_t> visited(removed.size(), 0);
    std::queue<int> pending;

    auto tryPush = [&](int x, int y) {
        int const index = y * width + x;
        if (visited[static_cast<std::size_t>(index)]) return;
        visited[static_cast<std::size_t>(index)] = 1;
        auto const pixel = sourcePixel(frame, static_cast<std::size_t>(index));
        bool const transparent = pixel.a < options.alphaThreshold;
        if (!transparent && colorDistanceSq(pixel, background) > maxDistance) return;
        removed[static_cast<std::size_t>(index)] = 1;
        pending.push(index);
    };

    for (int x = 0; x < width; ++x) {
        tryPush(x, 0);
        if (height > 1) tryPush(x, height - 1);
    }
    for (int y = 1; y + 1 < height; ++y) {
        tryPush(0, y);
        if (width > 1) tryPush(width - 1, y);
    }

    while (!pending.empty()) {
        int const index = pending.front();
        pending.pop();
        int const x = index % width;
        int const y = index / width;
        if (x > 0) tryPush(x - 1, y);
        if (x + 1 < width) tryPush(x + 1, y);
        if (y > 0) tryPush(x, y - 1);
        if (y + 1 < height) tryPush(x, y + 1);
    }
    return removed;
}

Pixel sampleNearest(
    SourceFrame const& frame,
    std::vector<std::uint8_t> const& removed,
    int sourceWidth,
    int sourceHeight,
    int x,
    int y,
    int width,
    int height,
    int alphaThreshold
) {
    int const sx = std::min(sourceWidth - 1, (2 * x + 1) * sourceWidth / (2 * width));
    int const sy = std::min(sourceHeight - 1, (2 * y + 1) * sourceHeight / (2 * height));
    std::size_t const index = static_cast<std::size_t>(sy) * sourceWidth + sx;
    if (removed[index]) return {};
    auto pixel = sourcePixel(frame, index);
    if (pixel.a < alphaThreshold) return {};
    return pixel;
}

Pixel sampleArea(
    SourceFrame const& frame,
    std::vector<std::uint8_t> const& removed,
    int sourceWidth,
    int sourceHeight,
    int x,
    int y,
    int width,
    int height,
    int alphaThreshold
) {
    int const x0 = x * sourceWidth / width;
    int const x1 = std::max(x0 + 1, ((x + 1) * sourceWidth + width - 1) / width);
    int const y0 = y * sourceHeight / height;
    int const y1 = std::max(y0 + 1, ((y + 1) * sourceHeight + height - 1) / height);

    std::uint64_t sumA = 0;
    std::uint64_t sumR = 0;
    std::uint64_t sumG = 0;
    std::uint64_t sumB = 0;
    int samples = 0;
    for (int sy = y0; sy < std::min(y1, sourceHeight); ++sy) {
        for (int sx = x0; sx < std::min(x1, sourceWidth); ++sx) {
            std::size_t const index = static_cast<std::size_t>(sy) * sourceWidth + sx;
            ++samples;
            if (removed[index]) continue;
            auto const pixel = sourcePixel(frame, index);
            sumA += pixel.a;
            sumR += static_cast<std::uint64_t>(pixel.r) * pixel.a;
            sumG += static_cast<std::uint64_t>(pixel.g) * pixel.a;
            sumB += static_cast<std::uint64_t>(pixel.b) * pixel.a;
        }
    }
    if (samples == 0 || sumA == 0) return {};
    int const alpha = static_cast<int>(sumA / static_cast<std::uint64_t>(samples));
    if (alpha < alphaThreshold) return {};
    return {
        static_cast<std::uint8_t>(sumR / sumA),
        static_cast<std::uint8_t>(sumG / sumA),
        static_cast<std::uint8_t>(sumB / sumA),
        static_cast<std::uint8_t>(std::min(alpha, 255))
    };
}

std::vector<ReducedFrame> reduceFrames(
    SourceAnimation const& source,
    std::vector<SelectedFrame> const& selected,
    int width,
    int height,
    Options const& options
) {
    std::vector<ReducedFrame> output;
    output.reserve(selected.size());
    for (auto const& selectedFrame : selected) {
        auto const& frame = *selectedFrame.frame;
        auto removed = backgroundMask(frame, source.width, source.height, options);
        ReducedFrame reduced;
        reduced.delayMs = selectedFrame.delayMs;
        reduced.pixels.resize(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                auto pixel = options.sampling == SamplingMode::Smooth
                    ? sampleArea(frame, removed, source.width, source.height,
                                 x, y, width, height, options.alphaThreshold)
                    : sampleNearest(frame, removed, source.width, source.height,
                                    x, y, width, height, options.alphaThreshold);
                reduced.pixels[static_cast<std::size_t>(y) * width + x] = pixel;
            }
        }
        output.push_back(std::move(reduced));
    }
    return output;
}

std::vector<Color> buildPalette(std::vector<ReducedFrame> const& frames, int maxColors) {
    struct Bin {
        std::uint64_t weight = 0;
        std::uint64_t r = 0;
        std::uint64_t g = 0;
        std::uint64_t b = 0;
    };
    std::vector<Bin> histogram(32768);
    for (auto const& frame : frames) {
        std::uint64_t const weight = static_cast<std::uint64_t>(std::clamp(frame.delayMs, 10, 1000));
        for (auto const& pixel : frame.pixels) {
            if (pixel.a == 0) continue;
            int const key = (pixel.r >> 3) << 10 | (pixel.g >> 3) << 5 | (pixel.b >> 3);
            auto& bin = histogram[static_cast<std::size_t>(key)];
            bin.weight += weight;
            bin.r += static_cast<std::uint64_t>(pixel.r) * weight;
            bin.g += static_cast<std::uint64_t>(pixel.g) * weight;
            bin.b += static_cast<std::uint64_t>(pixel.b) * weight;
        }
    }

    struct Entry {
        Color color;
        std::uint64_t weight = 0;
    };
    std::vector<Entry> entries;
    for (auto const& bin : histogram) {
        if (bin.weight == 0) continue;
        entries.push_back({{
            static_cast<std::uint8_t>(bin.r / bin.weight),
            static_cast<std::uint8_t>(bin.g / bin.weight),
            static_cast<std::uint8_t>(bin.b / bin.weight)
        }, bin.weight});
    }
    if (entries.empty()) return {};

    struct Box {
        std::vector<int> entries;
    };
    auto stats = [&](Box const& box) {
        struct Stats {
            int r0 = 255, r1 = 0, g0 = 255, g1 = 0, b0 = 255, b1 = 0;
            std::uint64_t weight = 0;
        } out;
        for (int index : box.entries) {
            auto const& entry = entries[static_cast<std::size_t>(index)];
            out.r0 = std::min(out.r0, static_cast<int>(entry.color.r));
            out.r1 = std::max(out.r1, static_cast<int>(entry.color.r));
            out.g0 = std::min(out.g0, static_cast<int>(entry.color.g));
            out.g1 = std::max(out.g1, static_cast<int>(entry.color.g));
            out.b0 = std::min(out.b0, static_cast<int>(entry.color.b));
            out.b1 = std::max(out.b1, static_cast<int>(entry.color.b));
            out.weight += entry.weight;
        }
        return out;
    };

    Box initial;
    initial.entries.resize(entries.size());
    std::iota(initial.entries.begin(), initial.entries.end(), 0);
    std::vector<Box> boxes;
    boxes.push_back(std::move(initial));

    while (static_cast<int>(boxes.size()) < maxColors) {
        int splitBox = -1;
        std::uint64_t bestScore = 0;
        int splitChannel = 0;
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
            if (boxes[static_cast<std::size_t>(i)].entries.size() < 2) continue;
            auto const s = stats(boxes[static_cast<std::size_t>(i)]);
            std::array<int, 3> ranges{s.r1 - s.r0, s.g1 - s.g0, s.b1 - s.b0};
            int const channel = static_cast<int>(std::max_element(ranges.begin(), ranges.end()) - ranges.begin());
            std::uint64_t const score = s.weight * static_cast<std::uint64_t>(ranges[static_cast<std::size_t>(channel)] + 1);
            if (score > bestScore) {
                bestScore = score;
                splitBox = i;
                splitChannel = channel;
            }
        }
        if (splitBox < 0) break;

        auto box = std::move(boxes[static_cast<std::size_t>(splitBox)]);
        std::sort(box.entries.begin(), box.entries.end(), [&](int a, int b) {
            auto const& ca = entries[static_cast<std::size_t>(a)].color;
            auto const& cb = entries[static_cast<std::size_t>(b)].color;
            if (splitChannel == 0) return ca.r < cb.r;
            if (splitChannel == 1) return ca.g < cb.g;
            return ca.b < cb.b;
        });
        std::uint64_t totalWeight = 0;
        for (int index : box.entries) totalWeight += entries[static_cast<std::size_t>(index)].weight;
        std::uint64_t cumulative = 0;
        std::size_t split = 1;
        for (; split + 1 < box.entries.size(); ++split) {
            cumulative += entries[static_cast<std::size_t>(box.entries[split - 1])].weight;
            if (cumulative * 2 >= totalWeight) break;
        }

        Box right;
        right.entries.assign(box.entries.begin() + static_cast<std::ptrdiff_t>(split), box.entries.end());
        box.entries.erase(box.entries.begin() + static_cast<std::ptrdiff_t>(split), box.entries.end());
        boxes[static_cast<std::size_t>(splitBox)] = std::move(box);
        boxes.push_back(std::move(right));
    }

    std::vector<Color> palette;
    palette.reserve(boxes.size());
    for (auto const& box : boxes) {
        std::uint64_t weight = 0, r = 0, g = 0, b = 0;
        for (int index : box.entries) {
            auto const& entry = entries[static_cast<std::size_t>(index)];
            weight += entry.weight;
            r += static_cast<std::uint64_t>(entry.color.r) * entry.weight;
            g += static_cast<std::uint64_t>(entry.color.g) * entry.weight;
            b += static_cast<std::uint64_t>(entry.color.b) * entry.weight;
        }
        if (weight == 0) continue;
        palette.push_back({
            static_cast<std::uint8_t>(r / weight),
            static_cast<std::uint8_t>(g / weight),
            static_cast<std::uint8_t>(b / weight)
        });
    }
    return palette;
}

int nearestColor(float r, float g, float b, std::vector<Color> const& palette) {
    int best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
        auto const& color = palette[static_cast<std::size_t>(i)];
        float const dr = r - color.r;
        float const dg = g - color.g;
        float const db = b - color.b;
        float const distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

std::vector<GridFrame> quantize(
    std::vector<ReducedFrame> const& reduced,
    std::vector<Color> const& palette,
    int width,
    int height,
    bool dither
) {
    std::vector<GridFrame> output;
    output.reserve(reduced.size());
    for (auto const& frame : reduced) {
        GridFrame grid;
        grid.delayMs = frame.delayMs;
        grid.cells.assign(static_cast<std::size_t>(width) * height, -1);
        std::vector<std::array<float, 3>> errors;
        if (dither) errors.resize(grid.cells.size());

        auto addError = [&](int x, int y, float r, float g, float b, float amount) {
            if (!dither || x < 0 || x >= width || y < 0 || y >= height) return;
            auto& error = errors[static_cast<std::size_t>(y) * width + x];
            error[0] += r * amount;
            error[1] += g * amount;
            error[2] += b * amount;
        };

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                std::size_t const index = static_cast<std::size_t>(y) * width + x;
                auto const& pixel = frame.pixels[index];
                if (pixel.a == 0) continue;
                float r = pixel.r;
                float g = pixel.g;
                float b = pixel.b;
                if (dither) {
                    r = std::clamp(r + errors[index][0], 0.f, 255.f);
                    g = std::clamp(g + errors[index][1], 0.f, 255.f);
                    b = std::clamp(b + errors[index][2], 0.f, 255.f);
                }
                int const colorIndex = nearestColor(r, g, b, palette);
                grid.cells[index] = static_cast<std::int16_t>(colorIndex);
                if (!dither) continue;
                auto const& chosen = palette[static_cast<std::size_t>(colorIndex)];
                float const er = r - chosen.r;
                float const eg = g - chosen.g;
                float const eb = b - chosen.b;
                addError(x + 1, y, er, eg, eb, 7.f / 16.f);
                addError(x - 1, y + 1, er, eg, eb, 3.f / 16.f);
                addError(x, y + 1, er, eg, eb, 5.f / 16.f);
                addError(x + 1, y + 1, er, eg, eb, 1.f / 16.f);
            }
        }
        if (!output.empty() && output.back().cells == grid.cells) {
            long long const delay = static_cast<long long>(output.back().delayMs) + grid.delayMs;
            output.back().delayMs = static_cast<int>(std::min<long long>(delay, std::numeric_limits<int>::max()));
        } else {
            output.push_back(std::move(grid));
        }
    }
    return output;
}

struct GeometryContext {
    ImportMode mode = ImportMode::Blocks;
    std::vector<std::vector<std::uint8_t>> obstacles;
    std::vector<int> ranks;
};

std::vector<Primitive> buildGeometry(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    GeometryContext const& context
) {
    switch (context.mode) {
        case ImportMode::Art:
            return vectorizeArt(
                positions, width, height, color,
                context.obstacles[static_cast<std::size_t>(color)]);
        case ImportMode::Paint:
            return vectorizePaint(
                positions, width, height, color,
                context.ranks[static_cast<std::size_t>(color)]);
        case ImportMode::Blocks:
            break;
    }
    return packBlocks(positions, width, height, color);
}

void sortByLayer(std::vector<Primitive>& objects) {
    std::stable_sort(objects.begin(), objects.end(),
                     [](Primitive const& left, Primitive const& right) {
                         return left.layer < right.layer;
                     });
}

std::vector<std::vector<std::uint8_t>> colorObstacles(
    std::vector<GridFrame> const& frames,
    int colors,
    int cells
) {
    std::vector<std::int16_t> first(static_cast<std::size_t>(cells), -1);
    std::vector<std::uint8_t> mixed(static_cast<std::size_t>(cells), 0);
    for (auto const& frame : frames) {
        for (int position = 0; position < cells; ++position) {
            int const color = frame.cells[static_cast<std::size_t>(position)];
            if (color < 0) continue;
            auto& seen = first[static_cast<std::size_t>(position)];
            if (seen < 0) {
                seen = static_cast<std::int16_t>(color);
            } else if (seen != color) {
                mixed[static_cast<std::size_t>(position)] = 1;
            }
        }
    }

    std::vector<std::vector<std::uint8_t>> result(
        static_cast<std::size_t>(colors),
        std::vector<std::uint8_t>(static_cast<std::size_t>(cells), 0));
    for (int color = 0; color < colors; ++color) {
        for (int position = 0; position < cells; ++position) {
            int const seen = first[static_cast<std::size_t>(position)];
            result[static_cast<std::size_t>(color)][static_cast<std::size_t>(position)] =
                seen >= 0 && (seen != color || mixed[static_cast<std::size_t>(position)]);
        }
    }
    return result;
}

bool maskBit(std::vector<std::uint64_t> const& mask, int frame) {
    return (mask[static_cast<std::size_t>(frame / 64)] & (std::uint64_t{1} << (frame % 64))) != 0;
}

bool allFrames(std::vector<std::uint64_t> const& mask, int frameCount) {
    for (int frame = 0; frame < frameCount; ++frame) {
        if (!maskBit(mask, frame)) return false;
    }
    return true;
}

std::size_t triggerCount(std::vector<VisibilityTrack> const& tracks, int frames, bool loop) {
    if (frames <= 1 || tracks.empty()) return 0;
    std::size_t count = 1;
    for (auto const& track : tracks) {
        if (!maskBit(track.mask, 0)) ++count;
    }
    for (int frame = 1; frame < frames; ++frame) {
        for (auto const& track : tracks) {
            if (maskBit(track.mask, frame) != maskBit(track.mask, frame - 1)) ++count;
        }
    }
    if (loop) {
        for (auto const& track : tracks) {
            if (maskBit(track.mask, frames - 1) != maskBit(track.mask, 0)) ++count;
        }
        count += static_cast<std::size_t>(frames);
    } else if (frames > 2) {
        count += static_cast<std::size_t>(frames - 2);
    }
    return count;
}

Candidate temporalCandidate(
    std::vector<GridFrame> const& frames,
    int width,
    int height,
    bool loop,
    GeometryContext const& context
) {
    int const frameCount = static_cast<int>(frames.size());
    int const words = (frameCount + 63) / 64;
    std::map<BucketKey, std::vector<int>> buckets;

    for (int position = 0; position < width * height; ++position) {
        std::map<int, std::vector<std::uint64_t>> local;
        for (int frame = 0; frame < frameCount; ++frame) {
            int const color = frames[static_cast<std::size_t>(frame)].cells[static_cast<std::size_t>(position)];
            if (color < 0) continue;
            auto [it, inserted] = local.try_emplace(color, static_cast<std::size_t>(words), 0);
            it->second[static_cast<std::size_t>(frame / 64)] |= std::uint64_t{1} << (frame % 64);
        }
        for (auto& [color, mask] : local) {
            buckets[{color, std::move(mask)}].push_back(position);
        }
    }

    Candidate candidate;
    candidate.strategy = "temporal";
    std::map<std::vector<std::uint64_t>, std::size_t> tracksByMask;
    for (auto const& [key, positions] : buckets) {
        auto objects = buildGeometry(positions, width, height, key.color, context);
        if (allFrames(key.mask, frameCount)) {
            candidate.staticObjects.insert(
                candidate.staticObjects.end(), objects.begin(), objects.end());
            continue;
        }
        auto [it, inserted] = tracksByMask.try_emplace(key.mask, candidate.tracks.size());
        if (inserted) candidate.tracks.push_back({key.mask, {}});
        auto& destination = candidate.tracks[it->second].objects;
        destination.insert(destination.end(), objects.begin(), objects.end());
    }
    sortByLayer(candidate.staticObjects);
    for (auto& track : candidate.tracks) sortByLayer(track.objects);
    candidate.triggers = triggerCount(candidate.tracks, frameCount, loop);
    return candidate;
}

Candidate frameCandidate(
    std::vector<GridFrame> const& frames,
    int width,
    int height,
    int colors,
    bool loop,
    GeometryContext const& context
) {
    int const frameCount = static_cast<int>(frames.size());
    int const words = (frameCount + 63) / 64;
    Candidate candidate;
    candidate.strategy = "por-frame";
    candidate.tracks.reserve(frames.size());

    std::vector<std::uint8_t> dynamic(static_cast<std::size_t>(width) * height, 0);
    std::vector<std::vector<int>> staticPositions(static_cast<std::size_t>(colors));
    for (int position = 0; position < width * height; ++position) {
        int const first = frames.front().cells[static_cast<std::size_t>(position)];
        bool fixed = true;
        for (int frame = 1; frame < frameCount; ++frame) {
            if (frames[static_cast<std::size_t>(frame)].cells[static_cast<std::size_t>(position)] != first) {
                fixed = false;
                break;
            }
        }
        if (!fixed) {
            dynamic[static_cast<std::size_t>(position)] = 1;
        } else if (first >= 0) {
            staticPositions[static_cast<std::size_t>(first)].push_back(position);
        }
    }
    for (int color = 0; color < colors; ++color) {
        auto objects = buildGeometry(
            staticPositions[static_cast<std::size_t>(color)], width, height, color, context);
        candidate.staticObjects.insert(
            candidate.staticObjects.end(), objects.begin(), objects.end());
    }
    sortByLayer(candidate.staticObjects);

    for (int frame = 0; frame < frameCount; ++frame) {
        VisibilityTrack track;
        track.mask.assign(static_cast<std::size_t>(words), 0);
        track.mask[static_cast<std::size_t>(frame / 64)] |= std::uint64_t{1} << (frame % 64);
        std::vector<std::vector<int>> positions(static_cast<std::size_t>(colors));
        auto const& cells = frames[static_cast<std::size_t>(frame)].cells;
        for (int position = 0; position < width * height; ++position) {
            if (!dynamic[static_cast<std::size_t>(position)]) continue;
            int const color = cells[static_cast<std::size_t>(position)];
            if (color >= 0) positions[static_cast<std::size_t>(color)].push_back(position);
        }
        for (int color = 0; color < colors; ++color) {
            auto objects = buildGeometry(
                positions[static_cast<std::size_t>(color)], width, height, color, context);
            track.objects.insert(track.objects.end(), objects.begin(), objects.end());
        }
        sortByLayer(track.objects);
        if (!track.objects.empty()) candidate.tracks.push_back(std::move(track));
    }
    candidate.triggers = triggerCount(candidate.tracks, frameCount, loop);
    return candidate;
}

Candidate chooseCandidate(Candidate temporal, Candidate perFrame, std::size_t objectBudget) {
    bool const temporalPlayable = temporal.triggers <= kPlaybackTriggerLimit;
    bool const perFramePlayable = perFrame.triggers <= kPlaybackTriggerLimit;
    if (temporalPlayable != perFramePlayable) {
        return temporalPlayable ? std::move(temporal) : std::move(perFrame);
    }

    bool const temporalFits = temporal.total() <= objectBudget;
    bool const perFrameFits = perFrame.total() <= objectBudget;
    if (temporalFits != perFrameFits) {
        return temporalFits ? std::move(temporal) : std::move(perFrame);
    }
    if (!temporalFits) {
        return temporal.total() <= perFrame.total() ? std::move(temporal) : std::move(perFrame);
    }
    if (temporal.runtimeCost() != perFrame.runtimeCost()) {
        return temporal.runtimeCost() < perFrame.runtimeCost()
            ? std::move(temporal)
            : std::move(perFrame);
    }
    return temporal.total() <= perFrame.total() ? std::move(temporal) : std::move(perFrame);
}

BuildResult buildAt(
    SourceAnimation const& source,
    Options const& options,
    int dimension,
    int frameLimit
) {
    int width = dimension;
    int height = dimension;
    if (source.width >= source.height) {
        height = std::max(1, static_cast<int>(std::lround(
            static_cast<double>(dimension) * source.height / source.width)));
    } else {
        width = std::max(1, static_cast<int>(std::lround(
            static_cast<double>(dimension) * source.width / source.height)));
    }

    auto selected = selectFrames(source, frameLimit);
    auto reduced = reduceFrames(source, selected, width, height, options);
    auto palette = buildPalette(reduced, options.maxColors);
    if (palette.empty()) return {{}, "El GIF quedo completamente transparente con estos ajustes."};
    auto frames = quantize(reduced, palette, width, height, options.dither);
    if (frames.empty()) return {{}, "No quedaron frames validos despues de procesar el GIF."};
    GeometryContext context;
    context.mode = options.mode;
    context.obstacles.assign(palette.size(), {});
    context.ranks.assign(palette.size(), 0);
    if (options.mode == ImportMode::Art) {
        context.obstacles = colorObstacles(
            frames, static_cast<int>(palette.size()), width * height);
    } else if (options.mode == ImportMode::Paint) {
        context.ranks = paintOrder(
            frames, static_cast<int>(palette.size()), width, height);
    }

    Candidate chosen;
    if (frames.size() == 1) {
        std::vector<std::vector<int>> positions(palette.size());
        for (int position = 0; position < width * height; ++position) {
            int const color = frames.front().cells[static_cast<std::size_t>(position)];
            if (color >= 0) positions[static_cast<std::size_t>(color)].push_back(position);
        }
        chosen.strategy = "estatico";
        for (int color = 0; color < static_cast<int>(palette.size()); ++color) {
            auto objects = buildGeometry(
                positions[static_cast<std::size_t>(color)], width, height, color, context);
            chosen.staticObjects.insert(
                chosen.staticObjects.end(), objects.begin(), objects.end());
        }
        sortByLayer(chosen.staticObjects);
    } else {
        auto temporal = temporalCandidate(frames, width, height, options.loop, context);
        auto perFrame = frameCandidate(
            frames, width, height, static_cast<int>(palette.size()), options.loop, context);
        chosen = chooseCandidate(std::move(temporal), std::move(perFrame), options.objectBudget);
    }

    ImportPlan plan;
    plan.width = width;
    plan.height = height;
    plan.sourceFrames = static_cast<int>(source.frames.size());
    plan.actualDimension = std::max(width, height);
    plan.mode = options.mode;
    std::size_t const visualObjects = chosen.visuals();
    plan.palette = std::move(palette);
    plan.frames = std::move(frames);
    plan.staticObjects = std::move(chosen.staticObjects);
    plan.tracks = std::move(chosen.tracks);
    plan.strategy = std::move(chosen.strategy);
    plan.visualObjects = visualObjects;
    plan.triggerObjects = chosen.triggers;
    plan.totalObjects = plan.visualObjects + plan.triggerObjects;

    auto countShape = [&](Primitive const& object) {
        switch (object.kind) {
            case PrimitiveKind::Block: ++plan.blockObjects; break;
            case PrimitiveKind::Stroke: ++plan.strokeObjects; break;
            case PrimitiveKind::Circle: ++plan.circleObjects; break;
            case PrimitiveKind::Triangle:
            case PrimitiveKind::WideTriangle: ++plan.triangleObjects; break;
        }
    };
    for (auto const& object : plan.staticObjects) countShape(object);
    for (auto const& track : plan.tracks) {
        for (auto const& object : track.objects) countShape(object);
    }
    return {std::move(plan), {}};
}

} // namespace

BuildResult buildPlan(SourceAnimation const& source, Options const& rawOptions) {
    if (source.width <= 0 || source.height <= 0 || source.frames.empty()) {
        return {{}, "El GIF no contiene una animacion valida."};
    }
    if (source.width > 4096 || source.height > 4096) {
        return {{}, "El GIF supera el limite de 4096 px por lado."};
    }
    std::size_t const expected = static_cast<std::size_t>(source.width) * source.height * 4;
    for (auto const& frame : source.frames) {
        if (frame.rgba.size() < expected) {
            return {{}, "Uno de los frames del GIF esta incompleto."};
        }
    }

    Options const options = sanitize(rawOptions);
    int dimension = options.maxDimension;
    int frameLimit = std::min(options.maxFrames, static_cast<int>(source.frames.size()));
    BuildResult result;

    for (int attempt = 0; attempt < 20; ++attempt) {
        result = buildAt(source, options, dimension, frameLimit);
        if (!result) return result;
        result.plan.requestedDimension = options.maxDimension;
        if (result.plan.totalObjects <= static_cast<std::size_t>(options.objectBudget) &&
            result.plan.triggerObjects <= kPlaybackTriggerLimit &&
            result.plan.tracks.size() + animationEventGroupCount(result.plan.frames.size(), options.loop) < 9800) {
            return result;
        }

        if (dimension > options.minDimension) {
            double const ratio = std::sqrt(
                static_cast<double>(options.objectBudget) /
                std::max<std::size_t>(result.plan.totalObjects, 1));
            int next = static_cast<int>(std::floor(dimension * std::clamp(ratio * 0.94, 0.5, 0.9)));
            dimension = std::max(options.minDimension, std::min(dimension - 1, next));
            continue;
        }
        if (frameLimit > 2) {
            double const ratio = static_cast<double>(options.objectBudget) /
                                 std::max<std::size_t>(result.plan.totalObjects, 1);
            int next = static_cast<int>(std::floor(frameLimit * std::clamp(ratio * 0.94, 0.5, 0.9)));
            frameLimit = std::max(2, std::min(frameLimit - 1, next));
            continue;
        }
        break;
    }
    return {{}, "No cabe en el presupuesto ni con la resolucion y frames minimos."};
}

} // namespace paimon::gifimport
