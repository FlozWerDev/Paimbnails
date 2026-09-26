#include "GifImportPipeline.hpp"
#include "ColorSpace.hpp"
#include "GifArtVectorizer.hpp"
#include "GifCircleVectorizer.hpp"
#include "GifFreeVectorizer.hpp"
#include "GifGlowPass.hpp"
#include "GifMotionPlanner.hpp"
#include "GifPaintVectorizer.hpp"
#include "GifParallel.hpp"
#include "GifStampCatalog.hpp"
#include "ImageWatermark.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <queue>
#include <utility>

namespace paimon::gifimport {

namespace {

constexpr std::size_t kPlaybackTriggerLimit = 512;
constexpr std::size_t kTriggerRuntimeWeight = 8;
constexpr float kPaintReviewGate = 95.f;
constexpr float kRenderQualityTarget = 97.f;

using StageProgress = std::function<void(BuildStage, float)>;

void report(StageProgress const& progress, BuildStage stage, float value) {
    if (progress) progress(stage, std::clamp(value, 0.f, 1.f));
}

StageProgress progressRange(
    BuildProgressCallback callback,
    float start,
    float length,
    int pass = 0,
    int passes = 0
) {
    if (!callback) return {};
    return [callback = std::move(callback), start, length, pass, passes](
               BuildStage stage, float value) {
        callback({stage, std::clamp(start + length * value, 0.f, 1.f), pass, passes});
    };
}

void finishProgress(BuildProgressCallback const& progress, int pass = 0, int passes = 0) {
    if (progress) progress({BuildStage::Done, 1.f, pass, passes});
}

constexpr int kPreviewScale = 2;
constexpr long long kPreviewIntervalMs = 250;

// tracing reports the half-done drawing; the popup only uploads the texture.
// no waiting: when another thread is already publishing, this notice is skipped.
struct PreviewThrottle {
    explicit PreviewThrottle(BuildPreviewCallback preview = {})
        : callback(std::move(preview)) {}

    BuildPreviewCallback callback;

    std::unique_lock<std::mutex> claim() {
        if (!callback) return {};
        std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
        if (!lock.owns_lock()) return {};
        auto const now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(kPreviewIntervalMs)) return {};
        last = now;
        return lock;
    }

    void publish(PreviewImage image) {
        if (!image.rgba.empty()) callback(std::move(image));
    }

private:
    std::mutex mutex;
    std::chrono::steady_clock::time_point last{};
};

PreviewImage gridPreviewImage(
    std::vector<std::int32_t> const& cells,
    std::vector<Color> const& palette,
    int width,
    int height
) {
    PreviewImage out{width, height,
        std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4, 0)};
    for (int i = 0; i < width * height; ++i) {
        int const color = cells[static_cast<std::size_t>(i)];
        if (color < 0 || color >= static_cast<int>(palette.size())) continue;
        auto const& chosen = palette[static_cast<std::size_t>(color)];
        std::size_t const pixel = static_cast<std::size_t>(i) * 4;
        out.rgba[pixel] = chosen.r;
        out.rgba[pixel + 1] = chosen.g;
        out.rgba[pixel + 2] = chosen.b;
        out.rgba[pixel + 3] = 255;
    }
    return out;
}

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
    std::vector<MotionTrack> motionTracks;
    std::size_t triggers = 0;
    std::string strategy;

    std::size_t visuals() const {
        std::size_t count = staticObjects.size();
        for (auto const& track : tracks) count += track.objects.size();
        for (auto const& track : motionTracks) count += track.objects.size();
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

// the UI sets the cap; here the range is only sanitized.
Options sanitize(Options options, std::size_t) {
    options.maxDimension = std::clamp(options.maxDimension, 4, 320);
    options.minDimension = std::clamp(options.minDimension, 4, options.maxDimension);
    options.maxColors = std::clamp(options.maxColors, 1, 64);
    options.maxFrames = std::clamp(options.maxFrames, 1, 120);
    options.objectBudget = std::clamp(options.objectBudget, 100, 50000);
    options.alphaThreshold = std::clamp(options.alphaThreshold, 1, 254);
    options.backgroundTolerance = std::clamp(options.backgroundTolerance, 0, 120);
    options.pixelSize = std::clamp(options.pixelSize, 1.f, 30.f);
    options.blurGlowDiameter = std::clamp(options.blurGlowDiameter, 2.f, 20.f);
    // Pixel follows the popup's Smooth/Pixel; default Smooth.
    // dither off: loses source-resolution analysis.
    if (options.mode == ImportMode::Paint || options.mode == ImportMode::Render ||
        options.mode == ImportMode::Free) {
        options.dither = false;
    } else if (options.mode != ImportMode::Blocks) {
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
    std::uint32_t borderSamples = 0;

    auto addBorder = [&](int x, int y) {
        auto const pixel = sourcePixel(frame, static_cast<std::size_t>(y) * width + x);
        if (pixel.a < options.alphaThreshold) return;
        ++borderSamples;
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
    // flat backgrounds only: in photos the top tone never reaches 1/3 of the edge.
    if (best->count * 3 < borderSamples) return removed;

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

    // never empty a solid image mistaken for background.
    std::size_t visible = 0;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < removed.size(); ++index) {
        auto const pixel = sourcePixel(frame, index);
        if (pixel.a < options.alphaThreshold) continue;
        ++visible;
        if (!removed[index]) ++kept;
    }
    if (visible > 0 && kept == 0) {
        std::fill(removed.begin(), removed.end(), 0);
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
    int const sx = std::min(
        sourceWidth - 1,
        static_cast<int>((2 * x + 1) * sourceWidth / (2 * width)));
    int const sy = std::min(
        sourceHeight - 1,
        static_cast<int>((2 * y + 1) * sourceHeight / (2 * height)));
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
    int const x0 = static_cast<int>(x * sourceWidth / width);
    int const x1 = std::max(
        x0 + 1,
        static_cast<int>(((x + 1) * sourceWidth + width - 1) / width));
    int const y0 = static_cast<int>(y * sourceHeight / height);
    int const y1 = std::max(
        y0 + 1,
        static_cast<int>(((y + 1) * sourceHeight + height - 1) / height));

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

// Analyzes at true resolution with a per-frame sample cap.
constexpr std::size_t kMaxAnalysisSamples = 4u << 20;

int analysisStride(int width, int height) {
    auto const pixels = static_cast<std::size_t>(width) * height;
    if (pixels <= kMaxAnalysisSamples) return 1;
    return static_cast<int>(std::ceil(
        std::sqrt(static_cast<double>(pixels) / kMaxAnalysisSamples)));
}

std::vector<std::vector<std::uint8_t>> backgroundMasks(
    SourceAnimation const& source,
    std::vector<SelectedFrame> const& selected,
    Options const& options
) {
    std::vector<std::vector<std::uint8_t>> masks;
    masks.reserve(selected.size());
    for (auto const& selectedFrame : selected) {
        masks.push_back(backgroundMask(
            *selectedFrame.frame, source.width, source.height, options));
    }
    return masks;
}

std::vector<ReducedFrame> reduceFrames(
    SourceAnimation const& source,
    std::vector<SelectedFrame> const& selected,
    std::vector<std::vector<std::uint8_t>> const& masks,
    int width,
    int height,
    Options const& options
) {
    std::vector<ReducedFrame> output;
    output.reserve(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) {
        auto const& selectedFrame = selected[index];
        auto const& frame = *selectedFrame.frame;
        auto const& removed = masks[index];
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

struct PaletteBin {
    std::uint64_t weight = 0;
    std::uint64_t r = 0;
    std::uint64_t g = 0;
    std::uint64_t b = 0;
};

using Histogram = std::vector<PaletteBin>;

Histogram emptyHistogram() { return Histogram(32768); }

void addSample(Histogram& histogram, Color color, std::uint64_t weight) {
    int const key = (color.r >> 3) << 10 | (color.g >> 3) << 5 | (color.b >> 3);
    auto& bin = histogram[static_cast<std::size_t>(key)];
    bin.weight += weight;
    bin.r += static_cast<std::uint64_t>(color.r) * weight;
    bin.g += static_cast<std::uint64_t>(color.g) * weight;
    bin.b += static_cast<std::uint64_t>(color.b) * weight;
}

int colorDistanceSq(Color const& first, Color const& second) {
    int const dr = static_cast<int>(first.r) - second.r;
    int const dg = static_cast<int>(first.g) - second.g;
    int const db = static_cast<int>(first.b) - second.b;
    return dr * dr + dg * dg + db * db;
}

int colorDistanceSq(Pixel const& pixel, Color const& color) {
    int const dr = static_cast<int>(pixel.r) - color.r;
    int const dg = static_cast<int>(pixel.g) - color.g;
    int const db = static_cast<int>(pixel.b) - color.b;
    return dr * dr + dg * dg + db * db;
}

constexpr int kFlatColorDistance = 20;

// orla is no true color: weighs by flat neighborhood.
template <typename Sample>
std::uint64_t flatnessWeight(Sample const& sample, int x, int y, int width, int height) {
    Color center;
    if (!sample(x, y, center)) return 0;
    int matching = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int const xx = x + dx;
            int const yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
            Color neighbor;
            if (!sample(xx, yy, neighbor)) continue;
            if (colorDistanceSq(center, neighbor) <=
                kFlatColorDistance * kFlatColorDistance) {
                ++matching;
            }
        }
    }
    return matching >= 8 ? 32 : matching >= 6 ? 8 : 1;
}

std::vector<Color> medianCut(Histogram const& histogram, int maxColors) {
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
    auto channel = [](Color const& color, int index) {
        return index == 0 ? color.r : index == 1 ? color.g : color.b;
    };
    // splits by error, not weight: avoids duplicating the dominant color.
    struct Spread {
        double error = 0.0;
        double weight = 0.0;
        int channel = 0;
    };
    auto spread = [&](Box const& box) {
        Spread out;
        std::array<double, 3> sum{};
        double weight = 0.0;
        for (int index : box.entries) {
            auto const& entry = entries[static_cast<std::size_t>(index)];
            weight += static_cast<double>(entry.weight);
            for (int c = 0; c < 3; ++c) {
                sum[static_cast<std::size_t>(c)] +=
                    static_cast<double>(channel(entry.color, c)) * entry.weight;
            }
        }
        if (weight <= 0.0) return out;
        std::array<double, 3> variance{};
        for (int index : box.entries) {
            auto const& entry = entries[static_cast<std::size_t>(index)];
            for (int c = 0; c < 3; ++c) {
                double const offset = channel(entry.color, c) -
                    sum[static_cast<std::size_t>(c)] / weight;
                variance[static_cast<std::size_t>(c)] +=
                    offset * offset * static_cast<double>(entry.weight);
            }
        }
        out.error = (variance[0] + variance[1] + variance[2]) / weight;
        out.weight = weight;
        out.channel = static_cast<int>(
            std::max_element(variance.begin(), variance.end()) - variance.begin());
        return out;
    };
    double totalHistogramWeight = 0.0;
    for (auto const& entry : entries) totalHistogramWeight += static_cast<double>(entry.weight);

    Box initial;
    initial.entries.resize(entries.size());
    std::iota(initial.entries.begin(), initial.entries.end(), 0);
    std::vector<Box> boxes;
    boxes.push_back(std::move(initial));

    while (static_cast<int>(boxes.size()) < maxColors) {
        int splitBox = -1;
        double bestError = 0.0;
        int splitChannel = 0;
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
            if (boxes[static_cast<std::size_t>(i)].entries.size() < 2) continue;
            auto const s = spread(boxes[static_cast<std::size_t>(i)]);
            // under 0.1% of the image earns no entry of its own.
            if (s.weight < totalHistogramWeight * 0.001) continue;
            if (s.error > bestError) {
                bestError = s.error;
                splitBox = i;
                splitChannel = s.channel;
            }
        }
        if (splitBox < 0) break;

        auto box = std::move(boxes[static_cast<std::size_t>(splitBox)]);
        std::sort(box.entries.begin(), box.entries.end(), [&](int a, int b) {
            return channel(entries[static_cast<std::size_t>(a)].color, splitChannel) <
                channel(entries[static_cast<std::size_t>(b)].color, splitChannel);
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

// Snaps each entry to the most-repeated flat tone and fuses equals.
std::vector<Color> refinePalette(Histogram const& histogram, std::vector<Color> palette) {
    if (palette.size() < 2) return palette;

    struct Center {
        OkLab lab;
        double weight = 0.0;
        double peak = 0.0;
        OkLab peakLab;
    };
    std::vector<Center> centers(palette.size());
    for (std::size_t i = 0; i < palette.size(); ++i) {
        centers[i].lab = rgbToOkLab(palette[i]);
    }

    for (auto const& bin : histogram) {
        if (bin.weight == 0) continue;
        Color const color{
            static_cast<std::uint8_t>(bin.r / bin.weight),
            static_cast<std::uint8_t>(bin.g / bin.weight),
            static_cast<std::uint8_t>(bin.b / bin.weight)
        };
        OkLab const lab = rgbToOkLab(color);
        std::size_t best = 0;
        float bestDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < centers.size(); ++i) {
            float const distance = oklabDistance(lab, centers[i].lab);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        auto& center = centers[best];
        double const weight = static_cast<double>(bin.weight);
        center.weight += weight;
        if (weight > center.peak) {
            center.peak = weight;
            center.peakLab = lab;
        }
    }

    for (auto& center : centers) {
        if (center.peak > 0.0) center.lab = center.peakLab;
    }

    bool merged = true;
    while (merged) {
        merged = false;
        std::size_t first = 0;
        std::size_t second = 1;
        float closest = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < centers.size(); ++i) {
            if (centers[i].weight <= 0.0) continue;
            for (std::size_t j = i + 1; j < centers.size(); ++j) {
                if (centers[j].weight <= 0.0) continue;
                float const distance = oklabDistance(centers[i].lab, centers[j].lab);
                if (distance < closest) {
                    closest = distance;
                    first = i;
                    second = j;
                }
            }
        }
        if (closest < kPaletteMinDistance) {
            double const total = centers[first].weight + centers[second].weight;
            double const ratio = centers[second].weight / total;
            centers[first].lab = {
                static_cast<float>(centers[first].lab.L * (1.0 - ratio) +
                    centers[second].lab.L * ratio),
                static_cast<float>(centers[first].lab.a * (1.0 - ratio) +
                    centers[second].lab.a * ratio),
                static_cast<float>(centers[first].lab.b * (1.0 - ratio) +
                    centers[second].lab.b * ratio)
            };
            centers[first].weight = total;
            centers[second].weight = 0.0;
            merged = true;
        }
    }

    std::vector<Color> refined;
    refined.reserve(centers.size());
    for (auto const& center : centers) {
        if (center.weight <= 0.0) continue;
        refined.push_back(oklabToRgb(center.lab));
    }
    return refined.empty() ? palette : refined;
}

int nearestColor(float r, float g, float b, std::vector<OkLab> const& paletteLabs) {
    OkLab const query = rgbToOkLab({
        static_cast<std::uint8_t>(std::clamp(std::lround(r), 0L, 255L)),
        static_cast<std::uint8_t>(std::clamp(std::lround(g), 0L, 255L)),
        static_cast<std::uint8_t>(std::clamp(std::lround(b), 0L, 255L))
    });
    int best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(paletteLabs.size()); ++i) {
        float const distance = oklabDistance(query, paletteLabs[static_cast<std::size_t>(i)]);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

// Single table quantizing every source pixel.
std::vector<std::int16_t> paletteLookup(std::vector<Color> const& palette) {
    std::vector<OkLab> labs;
    labs.reserve(palette.size());
    for (auto const& color : palette) labs.push_back(rgbToOkLab(color));

    std::vector<std::int16_t> lookup(32768, 0);
    for (int key = 0; key < 32768; ++key) {
        OkLab const query = rgbToOkLab({
            static_cast<std::uint8_t>(((key >> 10) & 31) << 3 | 4),
            static_cast<std::uint8_t>(((key >> 5) & 31) << 3 | 4),
            static_cast<std::uint8_t>((key & 31) << 3 | 4)
        });
        int best = 0;
        float bestDistance = std::numeric_limits<float>::max();
        for (int i = 0; i < static_cast<int>(labs.size()); ++i) {
            float const distance = oklabDistance(query, labs[static_cast<std::size_t>(i)]);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        lookup[static_cast<std::size_t>(key)] = static_cast<std::int16_t>(best);
    }
    return lookup;
}

// Votes the dominant color; averaging invents edge tones.
std::vector<GridFrame> quantizeFromSource(
    SourceAnimation const& source,
    std::vector<SelectedFrame> const& selected,
    std::vector<std::vector<std::uint8_t>> const& masks,
    std::vector<Color> const& palette,
    int width,
    int height,
    Options const& options
) {
    auto const lookup = paletteLookup(palette);
    int const stride = analysisStride(source.width, source.height);

    std::vector<GridFrame> output;
    output.reserve(selected.size());
    std::vector<std::uint32_t> votes(palette.size(), 0);
    for (std::size_t index = 0; index < selected.size(); ++index) {
        auto const& frame = *selected[index].frame;
        auto const& removed = masks[index];
        GridFrame grid;
        grid.delayMs = selected[index].delayMs;
        grid.cells.assign(static_cast<std::size_t>(width) * height, -1);
        for (int y = 0; y < height; ++y) {
            int const y0 = y * source.height / height;
            int const y1 = std::max(y0 + 1, ((y + 1) * source.height + height - 1) / height);
            for (int x = 0; x < width; ++x) {
                int const x0 = x * source.width / width;
                int const x1 = std::max(x0 + 1, ((x + 1) * source.width + width - 1) / width);
                std::fill(votes.begin(), votes.end(), 0);
                std::uint64_t sumA = 0;
                int samples = 0;
                for (int sy = y0; sy < std::min(y1, source.height); sy += stride) {
                    for (int sx = x0; sx < std::min(x1, source.width); sx += stride) {
                        std::size_t const position =
                            static_cast<std::size_t>(sy) * source.width + sx;
                        ++samples;
                        if (removed[position]) continue;
                        auto const pixel = sourcePixel(frame, position);
                        sumA += pixel.a;
                        if (pixel.a < options.alphaThreshold) continue;
                        int const key = (pixel.r >> 3) << 10 | (pixel.g >> 3) << 5 |
                            (pixel.b >> 3);
                        votes[static_cast<std::size_t>(
                            lookup[static_cast<std::size_t>(key)])] += pixel.a;
                    }
                }
                if (samples == 0) continue;
                if (sumA / static_cast<std::uint64_t>(samples) <
                    static_cast<std::uint64_t>(options.alphaThreshold)) {
                    continue;
                }
                auto const winner = std::max_element(votes.begin(), votes.end());
                if (*winner == 0) continue;
                grid.cells[static_cast<std::size_t>(y) * width + x] =
                    static_cast<std::int32_t>(std::distance(votes.begin(), winner));
            }
        }
        if (!output.empty() && output.back().cells == grid.cells) {
            long long const delay =
                static_cast<long long>(output.back().delayMs) + grid.delayMs;
            output.back().delayMs = static_cast<int>(
                std::min<long long>(delay, std::numeric_limits<int>::max()));
        } else {
            output.push_back(std::move(grid));
        }
    }
    return output;
}

std::vector<GridFrame> quantize(
    std::vector<ReducedFrame> const& reduced,
    std::vector<Color> const& palette,
    int width,
    int height,
    bool dither
) {
    std::vector<OkLab> paletteLabs;
    paletteLabs.reserve(palette.size());
    for (auto const& color : palette) paletteLabs.push_back(rgbToOkLab(color));

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
                int const colorIndex = nearestColor(r, g, b, paletteLabs);
                grid.cells[index] = static_cast<std::int32_t>(colorIndex);
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

// Source histogram; in Paint flatness weighs, not orla.
std::vector<Color> buildPalette(
    SourceAnimation const& source,
    std::vector<SelectedFrame> const& selected,
    std::vector<std::vector<std::uint8_t>> const& masks,
    Options const& options,
    int maxColors,
    int gridWidth,
    int gridHeight,
    bool flat
) {
    int const stride = analysisStride(source.width, source.height);
    // flatness measures at cell scale, not pixel.
    int const flatX = std::max(1, source.width / std::max(gridWidth, 1));
    int const flatY = std::max(1, source.height / std::max(gridHeight, 1));
    int const flatWidth = (source.width + flatX - 1) / flatX;
    int const flatHeight = (source.height + flatY - 1) / flatY;

    auto histogram = emptyHistogram();
    for (std::size_t index = 0; index < selected.size(); ++index) {
        auto const& frame = *selected[index].frame;
        auto const& removed = masks[index];
        auto const delay = static_cast<std::uint64_t>(
            std::clamp(selected[index].delayMs, 10, 1000));
        auto coarse = [&](int x, int y, Color& out) {
            std::size_t const position = static_cast<std::size_t>(
                std::min(y * flatY, source.height - 1)) * source.width +
                std::min(x * flatX, source.width - 1);
            if (removed[position]) return false;
            auto const pixel = sourcePixel(frame, position);
            if (pixel.a < options.alphaThreshold) return false;
            out = {pixel.r, pixel.g, pixel.b};
            return true;
        };

        std::vector<std::uint64_t> weights;
        if (flat) {
            weights.resize(static_cast<std::size_t>(flatWidth) * flatHeight);
            for (int y = 0; y < flatHeight; ++y) {
                for (int x = 0; x < flatWidth; ++x) {
                    weights[static_cast<std::size_t>(y) * flatWidth + x] =
                        flatnessWeight(coarse, x, y, flatWidth, flatHeight);
                }
            }
        }

        for (int y = 0; y < source.height; y += stride) {
            for (int x = 0; x < source.width; x += stride) {
                std::size_t const position =
                    static_cast<std::size_t>(y) * source.width + x;
                if (removed[position]) continue;
                auto const pixel = sourcePixel(frame, position);
                if (pixel.a < options.alphaThreshold) continue;
                std::uint64_t weight = delay;
                if (flat) {
                    weight *= std::max<std::uint64_t>(1, weights[
                        static_cast<std::size_t>(y / flatY) * flatWidth + x / flatX]);
                }
                addSample(histogram, {pixel.r, pixel.g, pixel.b}, weight);
            }
        }
    }
    return refinePalette(histogram, medianCut(histogram, maxColors));
}

constexpr int kSpeckleColorDistance = 50;
constexpr int kSmallPaletteSpeckleDistance = 65;

// Cost = distance over area: melts noise, keeps detail.
constexpr float kSpeckBudget = 0.25f;
// glow melts neighbor cells: Vert swallows specks that would show in flat.
// At x4 it eats eyes; x2 trims without touching them.
constexpr float kVertSpeckScale = 2.f;
// area cap: long thin lines survive.
constexpr int kSpeckArea = 12;

// Small blobs are dear: one object each while splitting the neighbor; melted.
void mergeFaintSpecks(
    std::vector<GridFrame>& frames,
    std::vector<Color> const& palette,
    int width,
    int height,
    float budget
) {
    if (palette.empty()) return;
    std::vector<OkLab> labs;
    labs.reserve(palette.size());
    for (auto const& color : palette) labs.push_back(rgbToOkLab(color));

    std::size_t const cells = static_cast<std::size_t>(width) * height;
    constexpr std::array<std::pair<int, int>, 4> neighbors{
        std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}
    };
    for (auto& frame : frames) {
        // ramps peel by layers: each pass eats the outer one.
        for (int pass = 0; pass < 8; ++pass) {
            std::vector<std::uint8_t> visited(cells, 0);
            bool changed = false;
            for (int start = 0; start < width * height; ++start) {
                int const color = frame.cells[static_cast<std::size_t>(start)];
                if (visited[static_cast<std::size_t>(start)]) continue;

                std::vector<int> component{start};
                visited[static_cast<std::size_t>(start)] = 1;
                for (std::size_t head = 0; head < component.size(); ++head) {
                    int const x = component[head] % width;
                    int const y = component[head] / width;
                    for (auto const [dx, dy] : neighbors) {
                        int const xx = x + dx;
                        int const yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                        int const neighbor = yy * width + xx;
                        if (visited[static_cast<std::size_t>(neighbor)]) continue;
                        if (frame.cells[static_cast<std::size_t>(neighbor)] != color) continue;
                        visited[static_cast<std::size_t>(neighbor)] = 1;
                        component.push_back(neighbor);
                    }
                }
                // walks the whole blob even past the cap.
                if (static_cast<int>(component.size()) > kSpeckArea) continue;

                // enclosed void: short alpha, not background; closed.
                if (color < 0) {
                    bool enclosed = true;
                    for (int position : component) {
                        int const x = position % width;
                        int const y = position / width;
                        if (x == 0 || y == 0 || x + 1 == width || y + 1 == height) {
                            enclosed = false;
                            break;
                        }
                    }
                    if (!enclosed) continue;
                }

                std::vector<int> border(palette.size(), 0);
                for (int position : component) {
                    int const x = position % width;
                    int const y = position / width;
                    for (auto const [dx, dy] : neighbors) {
                        int const xx = x + dx;
                        int const yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                        int const other = frame.cells[
                            static_cast<std::size_t>(yy) * width + xx];
                        if (other >= 0 && other != color) {
                            ++border[static_cast<std::size_t>(other)];
                        }
                    }
                }
                auto const winner = std::max_element(border.begin(), border.end());
                if (*winner == 0) continue;
                auto const replacement = static_cast<std::int32_t>(
                    std::distance(border.begin(), winner));
                if (color >= 0) {
                    float const cost = oklabDistance(
                        labs[static_cast<std::size_t>(color)],
                        labs[static_cast<std::size_t>(replacement)]) *
                        static_cast<float>(component.size());
                    if (cost > budget) continue;
                }
                for (int position : component) {
                    frame.cells[static_cast<std::size_t>(position)] = replacement;
                }
                changed = true;
            }
            if (!changed) break;
        }
    }
}

// Only fully-surrounded drops; the drawing edge shows.
void dissolveSpecks(
    std::vector<GridFrame>& frames,
    std::vector<Color> const& palette,
    int width,
    int height,
    int maxArea
) {
    if (palette.empty() || maxArea <= 0) return;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    constexpr std::array<std::pair<int, int>, 4> neighbors{
        std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}
    };
    for (auto& frame : frames) {
        for (int pass = 0; pass < 4; ++pass) {
            std::vector<std::uint8_t> visited(cells, 0);
            auto next = frame.cells;
            bool changed = false;
            for (int start = 0; start < width * height; ++start) {
                int const color = frame.cells[static_cast<std::size_t>(start)];
                if (color < 0 || visited[static_cast<std::size_t>(start)]) continue;

                std::vector<int> component;
                std::queue<int> pending;
                visited[static_cast<std::size_t>(start)] = 1;
                pending.push(start);
                bool tooBig = false;
                while (!pending.empty()) {
                    int const position = pending.front();
                    pending.pop();
                    component.push_back(position);
                    if (static_cast<int>(component.size()) > maxArea) {
                        tooBig = true;
                        break;
                    }
                    int const x = position % width;
                    int const y = position / width;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) continue;
                            int const xx = x + dx;
                            int const yy = y + dy;
                            if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                            int const neighbor = yy * width + xx;
                            if (visited[static_cast<std::size_t>(neighbor)]) continue;
                            if (frame.cells[static_cast<std::size_t>(neighbor)] != color) continue;
                            visited[static_cast<std::size_t>(neighbor)] = 1;
                            pending.push(neighbor);
                        }
                    }
                }
                if (tooBig) {
                    while (!pending.empty()) {
                        int const position = pending.front();
                        pending.pop();
                        int const x = position % width;
                        int const y = position / width;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dx == 0 && dy == 0) continue;
                                int const xx = x + dx;
                                int const yy = y + dy;
                                if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                                int const neighbor = yy * width + xx;
                                if (visited[static_cast<std::size_t>(neighbor)]) continue;
                                if (frame.cells[static_cast<std::size_t>(neighbor)] != color) continue;
                                visited[static_cast<std::size_t>(neighbor)] = 1;
                                pending.push(neighbor);
                            }
                        }
                    }
                    continue;
                }

                std::vector<int> votes(palette.size(), 0);
                int exposed = 0;
                int touching = 0;
                for (int position : component) {
                    int const x = position % width;
                    int const y = position / width;
                    for (auto const [dx, dy] : neighbors) {
                        int const xx = x + dx;
                        int const yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                        int const other = frame.cells[static_cast<std::size_t>(yy) * width + xx];
                        if (other < 0) {
                            ++exposed;
                        } else if (other != color) {
                            ++votes[static_cast<std::size_t>(other)];
                            ++touching;
                        }
                    }
                }
                // speck floating in void: background weave, erased.
                if (touching == 0) {
                    if (exposed == 0) continue;
                    for (int position : component) {
                        next[static_cast<std::size_t>(position)] = -1;
                    }
                    changed = true;
                    continue;
                }
                if (exposed > 0) continue;
                auto const winner = std::max_element(votes.begin(), votes.end());
                if (winner == votes.end() || *winner == 0) continue;
                auto const replacement = static_cast<std::int32_t>(
                    std::distance(votes.begin(), winner));
                for (int position : component) {
                    next[static_cast<std::size_t>(position)] = replacement;
                }
                changed = true;
            }
            frame.cells = std::move(next);
            if (!changed) break;
        }
    }
}

// Melts the speck into its most-present near-equal neighbor.
int nearbyReplacement(
    std::vector<int> const& votes,
    std::vector<Color> const& palette,
    int color,
    int maxDistanceSq,
    std::vector<std::uint8_t> const& allowed
) {
    int replacement = -1;
    int bestVotes = 0;
    int bestDistance = maxDistanceSq + 1;
    for (int other = 0; other < static_cast<int>(palette.size()); ++other) {
        int const support = votes[static_cast<std::size_t>(other)];
        if (support == 0 || (!allowed.empty() && !allowed[static_cast<std::size_t>(other)])) {
            continue;
        }
        int const distance = colorDistanceSq(
            palette[static_cast<std::size_t>(color)],
            palette[static_cast<std::size_t>(other)]);
        if (distance > maxDistanceSq) continue;
        if (support > bestVotes || (support == bestVotes && distance < bestDistance)) {
            replacement = other;
            bestVotes = support;
            bestDistance = distance;
        }
    }
    return replacement;
}

// Melts only when the color lands on the line between its neighbors.
int blendReplacement(
    std::vector<int> const& votes,
    std::vector<Color> const& palette,
    int color,
    int minimumJump
) {
    int first = -1;
    int second = -1;
    for (int other = 0; other < static_cast<int>(palette.size()); ++other) {
        if (votes[static_cast<std::size_t>(other)] == 0) continue;
        if (first < 0 || votes[static_cast<std::size_t>(other)] >
                votes[static_cast<std::size_t>(first)]) {
            second = first;
            first = other;
        } else if (second < 0 || votes[static_cast<std::size_t>(other)] >
                votes[static_cast<std::size_t>(second)]) {
            second = other;
        }
    }
    if (first < 0 || second < 0) return -1;

    auto distance = [&](int left, int right) {
        return std::sqrt(static_cast<float>(colorDistanceSq(
            palette[static_cast<std::size_t>(left)],
            palette[static_cast<std::size_t>(right)])));
    };
    float const jump = distance(first, second);
    if (jump < static_cast<float>(minimumJump)) return -1;
    float const toFirst = distance(color, first);
    float const toSecond = distance(color, second);
    if (toFirst + toSecond > jump * 1.3f) return -1;
    return toFirst <= toSecond ? first : second;
}

void compactPaintSpeckles(
    std::vector<GridFrame>& frames,
    std::vector<ReducedFrame> const& sourceFrames,
    std::vector<Color> const& palette,
    int width,
    int height
) {
    if (palette.empty()) return;
    bool const smallPalette = palette.size() <= 8;
    int const colorDistance = smallPalette
        ? kSmallPaletteSpeckleDistance : kSpeckleColorDistance;
    int const maxColorDistanceSq = colorDistance * colorDistance;
    int const dimension = std::max(width, height);
    int maxArea = dimension >= 120 ? 8 : dimension >= 80 ? 4 : 1;
    if (smallPalette) maxArea = std::max(maxArea * 2, 2);
    int const passes = smallPalette ? 4 : 1;
    std::size_t const cells = static_cast<std::size_t>(width) * height;
    constexpr std::array<std::pair<int, int>, 4> neighbors{
        std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1}, std::pair{0, 1}
    };

    bool const compareSource = sourceFrames.size() == frames.size();
    constexpr int maxErrorIncrease = 900;
    for (std::size_t frameIndex = 0; frameIndex < frames.size(); ++frameIndex) {
        auto& frame = frames[frameIndex];
        auto pixelReplacementFits = [&](int position, int from, int to) {
            if (smallPalette || !compareSource || from < 0 || to < 0) return true;
            auto const& pixel = sourceFrames[frameIndex].pixels[
                static_cast<std::size_t>(position)];
            int const increase = colorDistanceSq(
                pixel, palette[static_cast<std::size_t>(to)]) -
                colorDistanceSq(pixel, palette[static_cast<std::size_t>(from)]);
            return increase <= maxErrorIncrease;
        };
        auto replacementFits = [&](std::vector<int> const& component, int from, int to) {
            if (smallPalette || !compareSource || from < 0 || to < 0) return true;
            long long increase = 0;
            for (int position : component) {
                auto const& pixel = sourceFrames[frameIndex].pixels[
                    static_cast<std::size_t>(position)];
                increase += colorDistanceSq(
                    pixel, palette[static_cast<std::size_t>(to)]) -
                    colorDistanceSq(pixel, palette[static_cast<std::size_t>(from)]);
            }
            return increase <= static_cast<long long>(component.size()) * maxErrorIncrease;
        };
        // closes voids before measuring areas, so strokes never break.
        constexpr std::array<std::pair<int, int>, 4> gapDirections{
            std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}, std::pair{1, -1}
        };
        auto bridged = frame.cells;
        if (smallPalette) {
            for (int start = 0; start < width * height; ++start) {
                int const color = frame.cells[static_cast<std::size_t>(start)];
                if (color < 0) continue;
                int const startX = start % width;
                int const startY = start / width;
                for (auto const [dx, dy] : gapDirections) {
                    int const endX = startX + dx * 2;
                    int const endY = startY + dy * 2;
                    if (endX < 0 || endY < 0 || endX >= width || endY >= height) continue;
                    int const end = endY * width + endX;
                    if (frame.cells[static_cast<std::size_t>(end)] != color) continue;
                    int const gap = (startY + dy) * width + startX + dx;
                    int const gapColor = frame.cells[static_cast<std::size_t>(gap)];
                    if (gapColor >= 0 && colorDistanceSq(
                            palette[static_cast<std::size_t>(gapColor)],
                            palette[static_cast<std::size_t>(color)]) >
                            maxColorDistanceSq) {
                        continue;
                    }
                    bridged[static_cast<std::size_t>(gap)] =
                        static_cast<std::int32_t>(color);
                }
            }
        }
        frame.cells = std::move(bridged);
        bridged = frame.cells;
        for (int position = 0; position < width * height; ++position) {
            int const current = frame.cells[static_cast<std::size_t>(position)];
            int const x = position % width;
            int const y = position / width;
            int replacement = current;
            int bestDistance = maxColorDistanceSq + 1;
            for (auto const [dx, dy] : gapDirections) {
                int const x0 = x - dx;
                int const y0 = y - dy;
                int const x1 = x + dx;
                int const y1 = y + dy;
                if (x0 < 0 || y0 < 0 || x1 < 0 || y1 < 0 ||
                    x0 >= width || y0 >= height || x1 >= width || y1 >= height) {
                    continue;
                }
                int const first = frame.cells[static_cast<std::size_t>(y0) * width + x0];
                int const second = frame.cells[static_cast<std::size_t>(y1) * width + x1];
                if (first < 0 || first != second || first == current) continue;
                if (current < 0) {
                    replacement = first;
                    break;
                }
                int const distance = colorDistanceSq(
                    palette[static_cast<std::size_t>(current)],
                    palette[static_cast<std::size_t>(first)]);
                if (distance <= maxColorDistanceSq && distance < bestDistance &&
                    pixelReplacementFits(position, current, first)) {
                    replacement = first;
                    bestDistance = distance;
                }
            }
            if (replacement != current) {
                bridged[static_cast<std::size_t>(position)] =
                    static_cast<std::int32_t>(replacement);
            }
        }
        frame.cells = std::move(bridged);

        // each pass eats the outer thread and uncovers the next.
        for (int pass = 0; pass < passes; ++pass) {
            std::vector<std::uint8_t> visited(cells, 0);
            auto next = frame.cells;
            bool changed = false;
            for (int start = 0; start < width * height; ++start) {
                int const color = frame.cells[static_cast<std::size_t>(start)];
                if (color < 0 || visited[static_cast<std::size_t>(start)]) continue;

                std::vector<int> component;
                std::queue<int> pending;
                visited[static_cast<std::size_t>(start)] = 1;
                pending.push(start);
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
                            int const neighbor = yy * width + xx;
                            if (visited[static_cast<std::size_t>(neighbor)]) continue;
                            if (frame.cells[static_cast<std::size_t>(neighbor)] != color) continue;
                            visited[static_cast<std::size_t>(neighbor)] = 1;
                            pending.push(neighbor);
                        }
                    }
                }
                // filament: no cells ringed on all four sides.
                bool filament = true;
                for (int position : component) {
                    int const x = position % width;
                    int const y = position / width;
                    int inside = 0;
                    for (auto const [dx, dy] : neighbors) {
                        int const xx = x + dx;
                        int const yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                        int const neighbor = yy * width + xx;
                        inside += frame.cells[static_cast<std::size_t>(neighbor)] == color;
                    }
                    if (inside == static_cast<int>(neighbors.size())) {
                        filament = false;
                        break;
                    }
                }
                if (!filament && static_cast<int>(component.size()) > maxArea) continue;

                std::vector<int> votes(palette.size(), 0);
                for (int position : component) {
                    int const x = position % width;
                    int const y = position / width;
                    for (auto const [dx, dy] : neighbors) {
                        int const xx = x + dx;
                        int const yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= width || yy >= height) continue;
                        int const other = frame.cells[static_cast<std::size_t>(yy) * width + xx];
                        if (other >= 0 && other != color) {
                            ++votes[static_cast<std::size_t>(other)];
                        }
                    }
                }

                std::vector<std::uint8_t> allowed;
                if (!smallPalette && compareSource &&
                    static_cast<int>(component.size()) <= maxArea) {
                    allowed.assign(palette.size(), 1);
                    for (int other = 0; other < static_cast<int>(palette.size()); ++other) {
                        if (votes[static_cast<std::size_t>(other)] == 0) continue;
                        allowed[static_cast<std::size_t>(other)] =
                            replacementFits(component, color, other);
                    }
                }
                int replacement = static_cast<int>(component.size()) <= maxArea
                    ? nearbyReplacement(
                          votes, palette, color, maxColorDistanceSq, allowed)
                    : -1;
                if (replacement < 0 && filament &&
                    static_cast<int>(component.size()) <= maxArea * 4) {
                    replacement = blendReplacement(votes, palette, color, colorDistance);
                    if (replacement >= 0 &&
                        !replacementFits(component, color, replacement)) {
                        replacement = -1;
                    }
                }
                if (replacement < 0) continue;
                for (int position : component) {
                    next[static_cast<std::size_t>(position)] =
                        static_cast<std::int32_t>(replacement);
                }
                changed = true;
            }
            frame.cells = std::move(next);
            if (!changed) break;
        }
    }
}

struct GeometryContext {
    ImportMode mode = ImportMode::Blocks;
    bool quarterGlow = false;
    float glowDiameter = 4.f;
    // sampling lends color; never turns Paint into pixel output.
    bool gridExact = true;
    std::vector<std::vector<std::uint8_t>> obstacles;
    std::vector<int> ranks;
    std::vector<std::uint8_t> empty;
};

inline bool paintPathIsGridExact(ImportMode mode, SamplingMode sampling) {
    (void)sampling;
    return mode == ImportMode::Blocks;
}

std::vector<Primitive> buildGeometry(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    GeometryContext const& context
) {
    if (usesSoftGeometry(context.mode)) {
        std::vector<Primitive> objects;
        if (context.mode != ImportMode::Blur) {
            auto sorted = positions;
            std::sort(sorted.begin(), sorted.end());
            for (std::size_t i = 0; i < sorted.size();) {
                int const first = sorted[i];
                int const row = first / width;
                std::size_t end = i + 1;
                while (end < sorted.size() && sorted[end] / width == row &&
                    sorted[end] == sorted[end - 1] + 1) ++end;
                float const span = static_cast<float>(end - i);
                float const x = first % width + span * 0.5f;
                float const y = row + 0.5f;
                // facing ramps share even runs per horizontal span.
                objects.push_back({x, y + 0.5f, span, 1.f, 0.f,
                    static_cast<std::uint16_t>(color), PrimitiveKind::Stamp, 0, 1});
                objects.push_back({x, y - 0.5f, span, 1.f, 0.f,
                    static_cast<std::uint16_t>(color), PrimitiveKind::Stamp, 0, 2});
                i = end;
            }
        } else {
            float const size = context.glowDiameter;
            objects.reserve(positions.size() * (context.quarterGlow ? 4 : 1));
            for (int position : positions) {
                float const x = position % width + 0.5f;
                float const y = position / width + 0.5f;
                if (context.quarterGlow) {
                    float const half = size * 0.5f;
                    float const off = half * 0.5f;
                    constexpr float dx[]{-1.f, 1.f, 1.f, -1.f};
                    constexpr float dy[]{-1.f, -1.f, 1.f, 1.f};
                    for (int q = 0; q < 4; ++q) objects.push_back({x + dx[q] * off, y + dy[q] * off,
                        half, half, 0.f, static_cast<std::uint16_t>(color),
                        PrimitiveKind::Stamp, 0, static_cast<std::uint16_t>(3 + q)});
                } else {
                    objects.push_back({x, y, size, size, 0.f,
                        static_cast<std::uint16_t>(color), PrimitiveKind::Stamp, 0, 0});
                }
            }
        }
        return objects;
    }
    switch (context.mode) {
        case ImportMode::Art:
            return vectorizeArt(
                positions, width, height, color,
                context.obstacles[static_cast<std::size_t>(color)]);
        case ImportMode::Paint:
        case ImportMode::Render:
            return vectorizePaint(
                positions, width, height, color,
                context.ranks[static_cast<std::size_t>(color)],
                context.obstacles[static_cast<std::size_t>(color)],
                context.empty, context.gridExact);
        case ImportMode::Circles:
            return vectorizeCircles(
                positions, width, height, color,
                context.ranks[static_cast<std::size_t>(color)],
                context.obstacles[static_cast<std::size_t>(color)],
                context.empty);
        case ImportMode::Free:
            return vectorizeFree(
                positions, width, height, color,
                context.ranks[static_cast<std::size_t>(color)],
                context.obstacles[static_cast<std::size_t>(color)],
                context.empty, context.gridExact);
        case ImportMode::Blur:
        case ImportMode::Vert:
        case ImportMode::VertX:
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

void repairPaintSeams(
    std::vector<Primitive>& objects,
    std::vector<std::int32_t> const& cells,
    std::vector<int> const& ranks,
    int width,
    int height,
    bool gridExact = true
) {
    auto repairs = paintSeamRepairs(objects, cells, ranks, width, height, gridExact);
    if (repairs.empty()) return;
    objects.insert(objects.end(), repairs.begin(), repairs.end());
    sortByLayer(objects);
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

std::vector<std::vector<std::uint8_t>> paintObstacles(
    std::vector<GridFrame> const& frames,
    std::vector<int> const& ranks,
    int colors,
    int cells
) {
    std::vector<std::vector<std::uint8_t>> result(
        static_cast<std::size_t>(colors),
        std::vector<std::uint8_t>(static_cast<std::size_t>(cells), 0));
    std::vector<int> coveringRank(static_cast<std::size_t>(cells), colors);
    for (auto const& frame : frames) {
        for (int position = 0; position < cells; ++position) {
            auto& rank = coveringRank[static_cast<std::size_t>(position)];
            int const other = frame.cells[static_cast<std::size_t>(position)];
            if (other < 0) {
                rank = -1;
            } else if (rank >= 0) {
                rank = std::min(rank, ranks[static_cast<std::size_t>(other)]);
            }
        }
    }
    for (int color = 0; color < colors; ++color) {
        for (int position = 0; position < cells; ++position) {
            result[static_cast<std::size_t>(color)][static_cast<std::size_t>(position)] =
                coveringRank[static_cast<std::size_t>(position)] >
                ranks[static_cast<std::size_t>(color)];
        }
    }
    return result;
}

// Cells free in every frame: diagonal caps land peak-free.
std::vector<std::uint8_t> paintVoid(std::vector<GridFrame> const& frames, int cells) {
    std::vector<std::uint8_t> empty(static_cast<std::size_t>(cells), 1);
    for (auto const& frame : frames) {
        for (int position = 0; position < cells; ++position) {
            if (frame.cells[static_cast<std::size_t>(position)] >= 0) {
                empty[static_cast<std::size_t>(position)] = 0;
            }
        }
    }
    return empty;
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

std::size_t triggerCount(
    std::vector<VisibilityTrack> const& tracks,
    std::vector<MotionTrack> const& motion,
    int frames,
    bool loop
) {
    if (frames <= 1 || (tracks.empty() && motion.empty())) return 0;
    std::size_t count = 1 + motionTriggerCount(motion, frames, loop);
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
    GeometryContext const& context,
    std::vector<Color> const& palette,
    ImportMode mode,
    PreviewThrottle* preview = nullptr
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

    std::vector<std::pair<BucketKey const*, std::vector<int> const*>> entries;
    entries.reserve(buckets.size());
    for (auto const& entry : buckets) entries.emplace_back(&entry.first, &entry.second);
    std::vector<std::vector<Primitive>> traced(entries.size());
    std::vector<std::atomic<bool>> tracedDone(entries.size());
    for (auto& flag : tracedDone) flag.store(false, std::memory_order_relaxed);
    parallelFor(entries.size(), [&](std::size_t index) {
        traced[index] = buildGeometry(
            *entries[index].second, width, height, entries[index].first->color, context);
        tracedDone[index].store(true, std::memory_order_release);
        // soft modes read their molds from the final plan; half-built they come out blank.
        if (!preview || usesSoftGeometry(mode)) return;
        auto claim = preview->claim();
        if (!claim.owns_lock()) return;
        ImportPlan partial;
        partial.width = width;
        partial.height = height;
        partial.mode = mode;
        partial.palette = palette;
        partial.frames = {frames.front()};
        VisibilityTrack visible;
        visible.mask.assign(static_cast<std::size_t>(words), 0);
        visible.mask[0] = 1;
        for (std::size_t done = 0; done < entries.size(); ++done) {
            if (!tracedDone[done].load(std::memory_order_acquire)) continue;
            auto const& key = *entries[done].first;
            auto& target = allFrames(key.mask, frameCount)
                ? partial.staticObjects : visible.objects;
            target.insert(target.end(), traced[done].begin(), traced[done].end());
        }
        if (!visible.objects.empty()) partial.tracks.push_back(std::move(visible));
        auto pixels = renderPlanFrame(partial, 0, kPreviewScale, false);
        claim.unlock();
        preview->publish({width * kPreviewScale, height * kPreviewScale, std::move(pixels)});
    });

    Candidate candidate;
    candidate.strategy = "temporal";
    std::map<std::vector<std::uint64_t>, std::size_t> tracksByMask;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto const& key = *entries[index].first;
        auto const& objects = traced[index];
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
    // joint prune: one track never sees what's below.
    if (usesPaintGeometry(context.mode)) {
        prunePaintObjectsByVisibility(
            candidate.staticObjects, candidate.tracks, frameCount, width, height);
    }
    candidate.triggers = triggerCount(candidate.tracks, {}, frameCount, loop);
    return candidate;
}

Candidate frameCandidate(
    std::vector<GridFrame> const& frames,
    int width,
    int height,
    int colors,
    bool loop,
    GeometryContext const& context,
    std::vector<Color> const& palette,
    ImportMode mode,
    PreviewThrottle* preview = nullptr
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
    std::vector<std::vector<Primitive>> byColor(static_cast<std::size_t>(colors));
    std::vector<std::atomic<bool>> colorDone(static_cast<std::size_t>(colors));
    for (auto& flag : colorDone) flag.store(false, std::memory_order_relaxed);
    parallelFor(static_cast<std::size_t>(colors), [&](std::size_t color) {
        byColor[color] = buildGeometry(
            staticPositions[color], width, height, static_cast<int>(color), context);
        colorDone[color].store(true, std::memory_order_release);
        if (!preview || usesSoftGeometry(mode)) return;
        auto claim = preview->claim();
        if (!claim.owns_lock()) return;
        ImportPlan partial;
        partial.width = width;
        partial.height = height;
        partial.mode = mode;
        partial.palette = palette;
        partial.frames = {frames.front()};
        for (std::size_t done = 0; done < byColor.size(); ++done) {
            if (!colorDone[done].load(std::memory_order_acquire)) continue;
            partial.staticObjects.insert(
                partial.staticObjects.end(), byColor[done].begin(), byColor[done].end());
        }
        auto pixels = renderPlanFrame(partial, 0, kPreviewScale, false);
        claim.unlock();
        preview->publish({width * kPreviewScale, height * kPreviewScale, std::move(pixels)});
    });
    for (auto const& objects : byColor) {
        candidate.staticObjects.insert(
            candidate.staticObjects.end(), objects.begin(), objects.end());
    }
    sortByLayer(candidate.staticObjects);

    std::vector<VisibilityTrack> perFrame(static_cast<std::size_t>(frameCount));
    std::vector<std::atomic<bool>> frameDone(static_cast<std::size_t>(frameCount));
    for (auto& flag : frameDone) flag.store(false, std::memory_order_relaxed);
    parallelFor(static_cast<std::size_t>(frameCount), [&](std::size_t index) {
        int const frame = static_cast<int>(index);
        VisibilityTrack track;
        track.mask.assign(static_cast<std::size_t>(words), 0);
        track.mask[static_cast<std::size_t>(frame / 64)] |= std::uint64_t{1} << (frame % 64);
        std::vector<std::vector<int>> positions(static_cast<std::size_t>(colors));
        auto const& cells = frames[index].cells;
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
        perFrame[index] = std::move(track);
        frameDone[index].store(true, std::memory_order_release);
        if (!preview || usesSoftGeometry(mode)) return;
        if (!frameDone[0].load(std::memory_order_acquire)) return;
        auto claim = preview->claim();
        if (!claim.owns_lock()) return;
        ImportPlan partial;
        partial.width = width;
        partial.height = height;
        partial.mode = mode;
        partial.palette = palette;
        partial.frames = {frames.front()};
        for (auto const& objects : byColor) {
            partial.staticObjects.insert(
                partial.staticObjects.end(), objects.begin(), objects.end());
        }
        partial.tracks.push_back(perFrame[0]);
        auto pixels = renderPlanFrame(partial, 0, kPreviewScale, false);
        claim.unlock();
        preview->publish({width * kPreviewScale, height * kPreviewScale, std::move(pixels)});
    });
    for (auto& track : perFrame) {
        if (!track.objects.empty()) candidate.tracks.push_back(std::move(track));
    }
    if (usesPaintGeometry(context.mode)) {
        prunePaintObjectsByVisibility(
            candidate.staticObjects, candidate.tracks, frameCount, width, height);
    }
    candidate.triggers = triggerCount(candidate.tracks, {}, frameCount, loop);
    return candidate;
}

// Only the starting pose is traced; Moves carry the rest.
std::vector<MotionTrack> buildMotionTracks(
    std::vector<MotionGroup> const& groups,
    int width,
    int height,
    GeometryContext const& context
) {
    std::vector<MotionTrack> tracks;
    tracks.reserve(groups.size());
    for (auto const& group : groups) {
        MotionTrack track;
        track.mask = group.mask;
        track.keys = group.keys;
        std::map<int, std::vector<int>> byColor;
        for (std::size_t i = 0; i < group.positions.size(); ++i) {
            byColor[group.colors[i]].push_back(group.positions[i]);
        }
        for (auto const& [color, positions] : byColor) {
            auto objects = buildGeometry(positions, width, height, color, context);
            track.objects.insert(track.objects.end(), objects.begin(), objects.end());
        }
        sortByLayer(track.objects);
        for (auto& object : track.objects) {
            object.layer = static_cast<std::int16_t>(std::min(object.layer + 400, 999));
        }
        if (!track.objects.empty()) tracks.push_back(std::move(track));
    }
    return tracks;
}

// Moving pays when the total drops under the trigger cap.
bool worthMoving(Candidate const& plain, Candidate const& moved, std::size_t objectBudget) {
    if (moved.triggers > kPlaybackTriggerLimit) return false;
    if (moved.total() > objectBudget) return false;
    if (plain.total() > objectBudget) return true;
    return moved.total() * 20 < plain.total() * 19;
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

// Reindexes used molds so the library isn't dragged along.
void collectStamps(ImportPlan& plan) {
    // Free alone uses molds; the library is global.
    if (plan.mode != ImportMode::Free) return;
    auto const& variants = stampVariants();
    std::map<std::uint16_t, std::uint16_t> slots;
    auto remap = [&](Primitive& object) {
        if (object.kind != PrimitiveKind::Stamp) return;
        auto [slot, inserted] = slots.try_emplace(
            object.stamp, static_cast<std::uint16_t>(plan.stamps.size()));
        if (inserted) {
            plan.stamps.push_back(
                object.stamp < variants.size() ? variants[object.stamp].stamp : PlanStamp{});
        }
        object.stamp = slot->second;
    };
    for (auto& object : plan.staticObjects) remap(object);
    for (auto& track : plan.tracks) {
        for (auto& object : track.objects) remap(object);
    }
    for (auto& track : plan.motionTracks) {
        for (auto& object : track.objects) remap(object);
    }
}

float paintPlanSimilarity(
    ImportPlan const& plan,
    std::vector<GridFrame> const& reference,
    std::vector<ReducedFrame> const& sourceReference
) {
    float minimum = 100.f;
    int const frameCount = std::min(
        static_cast<int>(plan.frames.size()), static_cast<int>(reference.size()));
    bool const compareSource = sourceReference.size() == reference.size();
    for (int frame = 0; frame < frameCount; ++frame) {
        double score = 0.0;
        std::size_t compared = 0;
        auto const preview = renderPlanFrame(plan, frame, 1);
        auto const& cells = reference[static_cast<std::size_t>(frame)].cells;
        for (std::size_t position = 0; position < cells.size(); ++position) {
            std::size_t const pixel = position * 4;
            bool const visible = preview[pixel + 3] != 0;
            int const expected = cells[position];
            Pixel expectedPixel;
            bool sourceVisible = false;
            if (compareSource) {
                expectedPixel = sourceReference[static_cast<std::size_t>(frame)].pixels[position];
                sourceVisible = expectedPixel.a != 0;
            } else {
                sourceVisible = expected >= 0;
                if (sourceVisible && expected < static_cast<int>(plan.palette.size())) {
                    auto const& color = plan.palette[static_cast<std::size_t>(expected)];
                    expectedPixel = {color.r, color.g, color.b, 255};
                }
            }
            if (!sourceVisible && !visible) continue;
            ++compared;
            if (!sourceVisible || !visible) continue;
            double const dr = static_cast<double>(preview[pixel]) - expectedPixel.r;
            double const dg = static_cast<double>(preview[pixel + 1]) - expectedPixel.g;
            double const db = static_cast<double>(preview[pixel + 2]) - expectedPixel.b;
            score += 1.0 - std::sqrt(dr * dr + dg * dg + db * db) /
                (255.0 * std::sqrt(3.0));
        }
        if (compared > 0) {
            minimum = std::min(
                minimum, static_cast<float>(100.0 * score / compared));
        }
    }
    return minimum;
}

float sourcePlanSimilarity(
    ImportPlan const& plan,
    std::vector<ReducedFrame> const& reference,
    int scale
) {
    if (reference.size() != plan.frames.size()) return plan.similarity;

    float minimum = 100.f;
    std::size_t const pixels = static_cast<std::size_t>(plan.width) * scale *
        plan.height * scale;
    for (int frame = 0; frame < static_cast<int>(plan.frames.size()); ++frame) {
        auto const& expected = reference[static_cast<std::size_t>(frame)].pixels;
        if (expected.size() != pixels) return plan.similarity;

        auto const preview = renderPlanFrame(plan, frame, scale);
        double score = 0.0;
        std::size_t compared = 0;
        for (std::size_t position = 0; position < pixels; ++position) {
            std::size_t const pixel = position * 4;
            bool const visible = preview[pixel + 3] != 0;
            bool const sourceVisible = expected[position].a != 0;
            if (!visible && !sourceVisible) continue;
            ++compared;
            if (!visible || !sourceVisible) continue;

            double const dr = static_cast<double>(preview[pixel]) - expected[position].r;
            double const dg = static_cast<double>(preview[pixel + 1]) - expected[position].g;
            double const db = static_cast<double>(preview[pixel + 2]) - expected[position].b;
            score += 1.0 - std::sqrt(dr * dr + dg * dg + db * db) /
                (255.0 * std::sqrt(3.0));
        }
        if (compared > 0) {
            minimum = std::min(
                minimum, static_cast<float>(100.0 * score / compared));
        }
    }
    return minimum;
}

BuildResult buildAt(
    SourceAnimation const& source,
    Options const& options,
    int dimension,
    int frameLimit,
    bool compactSpeckles,
    StageProgress const& progress = {},
    PreviewThrottle* preview = nullptr
) {
    report(progress, BuildStage::Preparing, 0.f);
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
    report(progress, BuildStage::Preparing, 0.08f);
    auto const masks = backgroundMasks(source, selected, options);
    auto reduced = reduceFrames(source, selected, masks, width, height, options);
    report(progress, BuildStage::Resizing, 0.28f);
    auto palette = buildPalette(
        source, selected, masks, options, options.maxColors, width, height,
        usesPaintGeometry(options.mode));
    if (palette.empty()) return {{}, "El GIF quedo completamente transparente con estos ajustes."};
    report(progress, BuildStage::Palette, 0.4f);
    // dithered uses the reduced copy; plain decides from the original.
    auto frames = options.dither
        ? quantize(reduced, palette, width, height, true)
        : quantizeFromSource(
              source, selected, masks, palette, width, height, options);
    if (frames.empty()) return {{}, "No quedaron frames validos despues de procesar el GIF."};
    report(progress, BuildStage::Geometry, 0.5f);
    mergeFaintSpecks(frames, palette, width, height,
        options.mode == ImportMode::Vert || options.mode == ImportMode::VertX
            ? kSpeckBudget * kVertSpeckScale : kSpeckBudget);
    if (usesPaintGeometry(options.mode)) {
        // specks under 1/4000 of the drawing; tiny grids stay untouched.
        dissolveSpecks(
            frames, palette, width, height, std::min(width * height / 4000, 2));
        if (compactSpeckles) {
            compactPaintSpeckles(frames, reduced, palette, width, height);
        }
    }
    // geometry reviews against the clean grid, never the preview.
    if (preview) {
        preview->publish(gridPreviewImage(frames.front().cells, palette, width, height));
    }
    auto const referenceFrames = frames;
    GeometryContext context;
    context.mode = options.mode;
    context.gridExact = paintPathIsGridExact(options.mode, options.sampling);
    context.quarterGlow = options.mode == ImportMode::Blur &&
        options.softStamps.size() == 7 && !options.softStamps[0].objectId;
    context.glowDiameter = options.blurGlowDiameter;
    context.obstacles.assign(palette.size(), {});
    context.ranks.assign(palette.size(), 0);
    if (options.mode == ImportMode::Art) {
        context.obstacles = colorObstacles(
            frames, static_cast<int>(palette.size()), width * height);
    } else if (usesPaintGeometry(options.mode)) {
        context.ranks = paintOrder(
            frames, static_cast<int>(palette.size()), width, height);
        context.obstacles = paintObstacles(
            frames, context.ranks, static_cast<int>(palette.size()), width * height);
        context.empty = paintVoid(frames, width * height);
    }
    report(progress, BuildStage::Geometry, 0.62f);

    Candidate chosen;
    if (frames.size() == 1) {
        std::vector<std::vector<int>> positions(palette.size());
        for (int position = 0; position < width * height; ++position) {
            int const color = frames.front().cells[static_cast<std::size_t>(position)];
            if (color >= 0) positions[static_cast<std::size_t>(color)].push_back(position);
        }
        chosen.strategy = "estatico";
        std::vector<std::vector<Primitive>> byColor(palette.size());
        std::vector<std::atomic<bool>> colorDone(palette.size());
        for (auto& flag : colorDone) flag.store(false, std::memory_order_relaxed);
        parallelFor(palette.size(), [&](std::size_t color) {
            byColor[color] = buildGeometry(
                positions[color], width, height, static_cast<int>(color), context);
            colorDone[color].store(true, std::memory_order_release);
            if (!preview || usesSoftGeometry(options.mode)) return;
            auto claim = preview->claim();
            if (!claim.owns_lock()) return;
            ImportPlan partial;
            partial.width = width;
            partial.height = height;
            partial.mode = options.mode;
            partial.palette = palette;
            partial.frames = {frames.front()};
            for (std::size_t done = 0; done < byColor.size(); ++done) {
                if (!colorDone[done].load(std::memory_order_acquire)) continue;
                partial.staticObjects.insert(
                    partial.staticObjects.end(), byColor[done].begin(), byColor[done].end());
            }
            auto pixels = renderPlanFrame(partial, 0, kPreviewScale, false);
            claim.unlock();
            preview->publish({width * kPreviewScale, height * kPreviewScale, std::move(pixels)});
        });
        for (auto const& objects : byColor) {
            chosen.staticObjects.insert(
                chosen.staticObjects.end(), objects.begin(), objects.end());
        }
        sortByLayer(chosen.staticObjects);
        if (usesPaintGeometry(context.mode)) {
            prunePaintObjects(chosen.staticObjects, width, height);
            if (matchesGridExactly(context.mode)) {
                // repairs add fill: prune and repair again.
                repairPaintSeams(
                    chosen.staticObjects, frames.front().cells, context.ranks,
                    width, height, context.gridExact);
                prunePaintObjects(chosen.staticObjects, width, height);
                repairPaintSeams(
                    chosen.staticObjects, frames.front().cells, context.ranks,
                    width, height, context.gridExact);
                // stitched seams merge with their strips too.
                mergePaintSolids(chosen.staticObjects);
            }
        }
    } else {
        auto plan = [&](std::vector<GridFrame> const& source) {
            auto temporal = temporalCandidate(
                source, width, height, options.loop, context,
                palette, options.mode, preview);
            auto perFrame = frameCandidate(
                source, width, height, static_cast<int>(palette.size()),
                options.loop, context, palette, options.mode, preview);
            return chooseCandidate(
                std::move(temporal), std::move(perFrame), options.objectBudget);
        };
        chosen = plan(frames);

        MotionAnalysis motion;
        if (options.motion) motion = analyzeMotion(frames, width, height);
        // the moved plan competes; never replaces the fixed one.
        if (!motion.groups.empty()) {
            auto moved = plan(motion.residual);
            moved.motionTracks = buildMotionTracks(motion.groups, width, height, context);
            moved.strategy += "+move";
            moved.triggers = triggerCount(
                moved.tracks, moved.motionTracks,
                static_cast<int>(frames.size()), options.loop);
            if (worthMoving(chosen, moved, options.objectBudget)) chosen = std::move(moved);
        }
    }
    report(progress, BuildStage::Geometry, 0.84f);

    ImportPlan plan;
    plan.width = width;
    plan.height = height;
    plan.sourceFrames = static_cast<int>(source.frames.size());
    plan.actualDimension = std::max(width, height);
    plan.mode = options.mode;
    plan.palette = std::move(palette);
    plan.frames = std::move(frames);
    plan.staticObjects = std::move(chosen.staticObjects);
    plan.tracks = std::move(chosen.tracks);
    plan.motionTracks = std::move(chosen.motionTracks);
    plan.strategy = std::move(chosen.strategy);
    applyImageWatermark(plan, options.objectBudget);
    if (usesSoftGeometry(options.mode)) {
        plan.stamps = options.softStamps;
        plan.glowPaletteStart = 0;
        plan.glowOpacity = 1.f;
        if (options.mode == ImportMode::Blur) {
            auto const& mask = plan.stamps[context.quarterGlow ? 3 : 0].mask;
            double sum = 0.;
            for (auto alpha : mask.coverage) sum += alpha / 255.;
            // total glow never grows with diameter: compensated by area.
            float const glowArea = context.glowDiameter * context.glowDiameter;
            plan.glowOpacity = static_cast<float>(std::min(1.,
                mask.coverage.size() / std::max(glowArea * sum, 1.)));
        }
        // the wash carries the image's vertical flow: top/bottom averages
        // resolved to the palette, spending no new channels.
        if (options.mode == ImportMode::VertX && options.gradientWash) {
            auto washIndex = [&](bool top) {
                int sumR = 0, sumG = 0, sumB = 0, count = 0;
                int const y0 = top ? 0 : plan.height * 3 / 4;
                int const y1 = top ? (plan.height + 3) / 4 : plan.height;
                for (int y = y0; y < y1; ++y) for (int x = 0; x < plan.width; ++x) {
                    int const index = plan.frames.front().cells[
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(plan.width) + x];
                    if (index < 0 || index >= static_cast<int>(plan.palette.size())) continue;
                    auto const& color = plan.palette[static_cast<std::size_t>(index)];
                    sumR += color.r; sumG += color.g; sumB += color.b; ++count;
                }
                if (!count) return -1;
                Color const average{
                    static_cast<std::uint8_t>(sumR / count),
                    static_cast<std::uint8_t>(sumG / count),
                    static_cast<std::uint8_t>(sumB / count)};
                int best = -1, bestDist = std::numeric_limits<int>::max();
                for (std::size_t i = 0; i < plan.palette.size(); ++i) {
                    int const dist = colorDistanceSq(average, plan.palette[i]);
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = static_cast<int>(i);
                    }
                }
                return best;
            };
            int const top = washIndex(true);
            int const bottom = washIndex(false);
            if (top >= 0 && bottom >= 0) {
                plan.gradientWash = true;
                plan.washTop = top;
                plan.washBottom = bottom;
                ++chosen.triggers;
            }
        }
        if (options.softBackdrop) {
            plan.softBackdropColor = static_cast<int>(plan.palette.size());
            plan.palette.push_back({0, 0, 0});
            plan.staticObjects.insert(plan.staticObjects.begin(), {
                width * 0.5f, height * 0.5f, static_cast<float>(width), static_cast<float>(height),
                0.f, static_cast<std::uint16_t>(plan.softBackdropColor), PrimitiveKind::Block, -999});
        }
        // diameter stays in the strategy string for bench duel comparison.
        char glowTag[16] = "vert/";
        if (options.mode == ImportMode::Blur) {
            std::snprintf(glowTag, sizeof(glowTag), "blur/%dx/",
                static_cast<int>(context.glowDiameter));
        } else if (options.mode == ImportMode::VertX) {
            std::snprintf(glowTag, sizeof(glowTag), "vertx/");
        }
        plan.strategy = glowTag + plan.strategy;
    } else {
        collectStamps(plan);
    }
    plan.visualObjects = plan.staticObjects.size();
    for (auto const& track : plan.tracks) plan.visualObjects += track.objects.size();
    for (auto const& track : plan.motionTracks) plan.visualObjects += track.objects.size();
    plan.triggerObjects = chosen.triggers;
    plan.moveTriggers = motionMoveCount(
        plan.motionTracks, static_cast<int>(plan.frames.size()), options.loop);
    plan.totalObjects = plan.visualObjects + plan.triggerObjects;

    auto countShape = [&](Primitive const& object) {
        switch (object.kind) {
            case PrimitiveKind::Block: ++plan.blockObjects; break;
            case PrimitiveKind::Stroke: ++plan.strokeObjects; break;
            case PrimitiveKind::Circle: ++plan.circleObjects; break;
            case PrimitiveKind::Triangle:
            case PrimitiveKind::WideTriangle: ++plan.triangleObjects; break;
            case PrimitiveKind::Glow: ++plan.glowObjects; break;
            case PrimitiveKind::Stamp: ++plan.stampObjects; break;
        }
    };
    for (auto const& object : plan.staticObjects) countShape(object);
    for (auto const& track : plan.tracks) {
        for (auto const& object : track.objects) countShape(object);
    }
    for (auto const& track : plan.motionTracks) {
        for (auto const& object : track.objects) countShape(object);
    }
    // review compares frame by frame and takes a while; the drawing is ready.
    if (preview) {
        auto pixels = renderPlanFrame(plan, 0, kPreviewScale, false);
        preview->publish({
            plan.width * kPreviewScale, plan.height * kPreviewScale, std::move(pixels)});
    }
    if (usesPaintGeometry(plan.mode)) {
        report(progress, BuildStage::Reviewing, 0.9f);
        plan.geometrySimilarity = paintPlanSimilarity(plan, referenceFrames, {});
        plan.similarity = paintPlanSimilarity(plan, referenceFrames, reduced);
        plan.detailSimilarity = plan.similarity;
        if (plan.mode == ImportMode::Render && plan.frames.size() == selected.size()) {
            auto detailed = reduceFrames(
                source, selected, masks, width * 2, height * 2, options);
            plan.detailSimilarity = sourcePlanSimilarity(plan, detailed, 2);
        }
    }
    report(progress, BuildStage::Reviewing, 1.f);
    return {std::move(plan), {}};
}

bool planFits(ImportPlan const& plan, Options const& options) {
    return plan.totalObjects <= static_cast<std::size_t>(options.objectBudget) &&
        plan.triggerObjects <= kPlaybackTriggerLimit &&
        plan.tracks.size() + plan.motionTracks.size() +
            animationEventGroupCount(plan.frames.size(), options.loop) < 9800;
}

std::vector<int> renderDimensions(Options const& options) {
    constexpr std::array ratios{0.45, 0.6, 0.72, 0.84, 0.93, 1.0};
    std::vector<int> dimensions;
    auto add = [&](int dimension) {
        dimension = std::clamp(dimension, options.minDimension, options.maxDimension);
        if (std::find(dimensions.begin(), dimensions.end(), dimension) == dimensions.end()) {
            dimensions.push_back(dimension);
        }
    };

    add(options.minDimension);
    for (double ratio : ratios) {
        add(static_cast<int>(std::lround(options.maxDimension * ratio)));
    }
    return dimensions;
}

float renderQuality(ImportPlan const& plan) {
    return std::min(plan.similarity, plan.detailSimilarity);
}

bool betterRenderPlan(
    ImportPlan const& candidate,
    ImportPlan const& best,
    std::size_t softLimit
) {
    float const quality = renderQuality(candidate);
    float const bestQuality = renderQuality(best);
    bool const candidateFitsSoftLimit = candidate.totalObjects <= softLimit;
    bool const bestFitsSoftLimit = best.totalObjects <= softLimit;
    if (!bestFitsSoftLimit && candidateFitsSoftLimit &&
        quality + 0.75f >= bestQuality) {
        return true;
    }
    if (std::abs(quality - bestQuality) <= 0.25f) {
        return candidate.totalObjects < best.totalObjects;
    }
    if (quality <= bestQuality) return false;
    if (candidateFitsSoftLimit) return true;
    if (bestFitsSoftLimit && bestQuality >= kRenderQualityTarget) return false;
    return bestQuality < kRenderQualityTarget;
}

BuildResult buildRenderPlan(
    SourceAnimation const& source,
    Options const& options,
    int frameLimit,
    BuildProgressCallback const& progress,
    PreviewThrottle* preview = nullptr
) {
    auto const dimensions = renderDimensions(options);
    int const passes = static_cast<int>(dimensions.size());
    std::size_t const softLimit = std::min<std::size_t>(
        options.objectBudget, source.frames.size() > 1 ? 6000 : 2500);

    // independent passes in parallel; ordered pick after.
    std::vector<BuildResult> results(static_cast<std::size_t>(passes));
    std::vector<std::atomic<float>> shares(static_cast<std::size_t>(passes));
    std::atomic<int> done{0};
    std::mutex reporting;
    float published = 0.f;
    // the bar never goes back: summed under lock.
    auto publish = [&] {
        if (!progress) return;
        std::lock_guard<std::mutex> lock(reporting);
        float total = 0.f;
        for (auto const& share : shares) total += share.load(std::memory_order_relaxed);
        float const value = 0.01f + 0.97f * total / static_cast<float>(passes);
        if (value < published) return;
        published = value;
        progress({
            BuildStage::Refining, value, done.load(std::memory_order_relaxed), passes});
    };

    parallelFor(static_cast<std::size_t>(passes), [&](std::size_t index) {
        int const dimension = dimensions[index];
        auto share = [&, index](BuildStage, float value) {
            if (value <= shares[index].load(std::memory_order_relaxed)) return;
            shares[index].store(value, std::memory_order_relaxed);
            publish();
        };
        auto result = buildAt(source, options, dimension, frameLimit, true, share, preview);
        if (result && result.plan.geometrySimilarity < kPaintReviewGate) {
            auto plain = buildAt(source, options, dimension, frameLimit, false, share, preview);
            if (plain && plain.plan.geometrySimilarity > result.plan.geometrySimilarity) {
                result = std::move(plain);
            }
        }
        if (result) result.plan.requestedDimension = options.maxDimension;
        shares[index].store(1.f, std::memory_order_relaxed);
        done.fetch_add(1, std::memory_order_relaxed);
        publish();
        results[index] = std::move(result);
    });

    ImportPlan best;
    bool hasBest = false;
    int attempted = 0;
    for (auto& result : results) {
        ++attempted;
        if (!result) {
            finishProgress(progress, attempted, passes);
            return std::move(result);
        }
        if (!planFits(result.plan, options)) continue;
        if (!hasBest || betterRenderPlan(result.plan, best, softLimit)) {
            best = std::move(result.plan);
            hasBest = true;
        }
    }

    finishProgress(progress, attempted, passes);
    if (!hasBest) {
        return {{}, "Render no encontro un resultado que entre en el presupuesto."};
    }
    best.renderPasses = attempted;
    best.strategy = "render/" + best.strategy;
    return {std::move(best), {}};
}

BuildResult buildRegularPlan(
    SourceAnimation const& source,
    Options const& options,
    int frameLimit,
    BuildProgressCallback const& progress,
    PreviewThrottle* preview = nullptr
) {
    int dimension = options.maxDimension;
    BuildResult result;
    ImportPlan best;
    bool hasBest = false;

    for (int attempt = 0; attempt < 20; ++attempt) {
        float const start = attempt == 0
            ? 0.01f
            : 0.75f + 0.23f * (attempt - 1) / 19.f;
        float const length = attempt == 0 ? 0.74f : 0.23f / 19.f;
        auto compactProgress = progressRange(progress, start, length * 0.68f);
        result = buildAt(
            source, options, dimension, frameLimit, true, compactProgress, preview);
        if (!result) {
            finishProgress(progress);
            return result;
        }
        result.plan.requestedDimension = options.maxDimension;
        if (matchesGridExactly(options.mode) &&
            result.plan.geometrySimilarity < kPaintReviewGate) {
            auto plainProgress = progressRange(
                progress, start + length * 0.68f, length * 0.3f);
            auto plain = buildAt(
                source, options, dimension, frameLimit, false, plainProgress, preview);
            if (plain && plain.plan.geometrySimilarity > result.plan.geometrySimilarity) {
                result = std::move(plain);
                result.plan.requestedDimension = options.maxDimension;
            }
        }
        if (planFits(result.plan, options)) {
            if (!matchesGridExactly(options.mode) ||
                result.plan.geometrySimilarity >= kPaintReviewGate ||
                (attempt == 0 && result.plan.geometrySimilarity >= 90.f)) {
                finishProgress(progress);
                return result;
            }
            bool const improved = !hasBest ||
                result.plan.geometrySimilarity > best.geometrySimilarity;
            if (improved) {
                best = result.plan;
                hasBest = true;
            }
            if (!improved || dimension <= options.minDimension) {
                finishProgress(progress);
                return {std::move(best), {}};
            }
            dimension = std::max(
                options.minDimension, static_cast<int>(std::floor(dimension * 0.9)));
            continue;
        }

        if (dimension > options.minDimension) {
            double const ratio = std::sqrt(
                static_cast<double>(options.objectBudget) /
                std::max<std::size_t>(result.plan.totalObjects, 1));
            int next = static_cast<int>(std::floor(
                dimension * std::clamp(ratio * 0.94, 0.5, 0.9)));
            dimension = std::max(
                options.minDimension, std::min(dimension - 1, next));
            continue;
        }
        if (frameLimit > 2) {
            double const ratio = static_cast<double>(options.objectBudget) /
                                 std::max<std::size_t>(result.plan.totalObjects, 1);
            int next = static_cast<int>(std::floor(
                frameLimit * std::clamp(ratio * 0.94, 0.5, 0.9)));
            frameLimit = std::max(2, std::min(frameLimit - 1, next));
            continue;
        }
        break;
    }
    finishProgress(progress);
    if (hasBest) return {std::move(best), {}};
    return {{}, "No cabe en el presupuesto ni con la resolucion y frames minimos."};
}

// Motion goes last: its edge would poison the search.
BuildResult tryMotionPlan(
    SourceAnimation const& source,
    Options const& options,
    BuildResult best
) {
    if (!best || !best.plan.animated()) return best;
    auto motionOptions = options;
    motionOptions.motion = true;
    auto moved = buildAt(
        source, motionOptions, best.plan.actualDimension,
        static_cast<int>(best.plan.frames.size()), true);
    if (!moved || moved.plan.motionTracks.empty()) return best;
    if (moved.plan.totalObjects * 20 >= best.plan.totalObjects * 19) return best;
    if (usesPaintGeometry(options.mode) &&
        moved.plan.similarity + 1.5f < best.plan.similarity) {
        return best;
    }
    moved.plan.requestedDimension = best.plan.requestedDimension;
    moved.plan.renderPasses = best.plan.renderPasses;
    if (best.plan.strategy.starts_with("render/")) {
        moved.plan.strategy = "render/" + moved.plan.strategy;
    }
    return moved;
}

} // namespace

BuildResult buildPlan(
    SourceAnimation const& source,
    Options const& rawOptions,
    BuildProgressCallback progress,
    BuildPreviewCallback preview
) {
    if (progress) progress({BuildStage::Preparing, 0.f, 0, 0});
    if (source.width <= 0 || source.height <= 0 || source.frames.empty()) {
        finishProgress(progress);
        return {{}, "El GIF no contiene una animacion valida."};
    }
    if (source.width > 4096 || source.height > 4096) {
        finishProgress(progress);
        return {{}, "El GIF supera el limite de 4096 px por lado."};
    }
    std::size_t const expected = static_cast<std::size_t>(source.width) * source.height * 4;
    for (auto const& frame : source.frames) {
        if (frame.rgba.size() < expected) {
            finishProgress(progress);
            return {{}, "Uno de los frames del GIF esta incompleto."};
        }
    }

    Options const options = sanitize(rawOptions, source.frames.size());
    if (usesSoftGeometry(options.mode)) {
        auto validStamp = [&](std::size_t index) {
            if (index >= options.softStamps.size()) return false;
            auto const& stamp = options.softStamps[index];
            return stamp.objectId > 0 && stamp.baseWidth > 0.f && stamp.baseHeight > 0.f &&
                stamp.mask.width > 0 && stamp.mask.height > 0 &&
                stamp.mask.coverage.size() == static_cast<std::size_t>(stamp.mask.width) * stamp.mask.height;
        };
        bool const valid = options.softStamps.size() == 7 && (options.mode == ImportMode::Blur
            ? (options.softStamps[0].objectId > 0 ? validStamp(0)
                : (validStamp(3) && validStamp(4) && validStamp(5) && validStamp(6)))
            : (validStamp(1) && validStamp(2) && !options.softStamps[1].analyticFallback &&
                !options.softStamps[2].analyticFallback));
        if (!valid) {
            // only reachable with no toolbox, or no native nor spare.
            std::string missing;
            auto const flag = [&](std::size_t index, char const* label) {
                if (!validStamp(index)) {
                    if (!missing.empty()) missing += ", ";
                    missing += label;
                }
            };
            if (options.softStamps.size() != 7) {
                missing = "biblioteca vacia (toolbox no disponible)";
            } else if (options.mode == ImportMode::Blur) {
                if (options.softStamps[0].objectId > 0) flag(0, "0/radial");
                else {
                    flag(3, "3/cuarto-sup-izq"); flag(4, "4/cuarto-sup-der");
                    flag(5, "5/cuarto-inf-der"); flag(6, "6/cuarto-inf-izq");
                }
            } else {
                flag(1, "1/rampa-desc"); flag(2, "2/rampa-asc");
            }
            char detail[256];
            std::snprintf(detail, sizeof(detail),
                " (errores nativos radial=%.4f vertical=%.4f cuartos=%.4f; "
                "ver 'Native soft shapes' en el log)",
                options.softMatchErrors[0], options.softMatchErrors[1],
                options.softMatchErrors[2]);
            finishProgress(progress);
            if ((options.mode == ImportMode::Vert || options.mode == ImportMode::VertX) &&
                options.softStamps.size() == 7 &&
                (options.softStamps[1].analyticFallback || options.softStamps[2].analyticFallback)) {
                // spare 3637 stretched to span x 1 paints discs, not ramps.
                return {{}, "El modo " +
                    std::string(options.mode == ImportMode::VertX ? "VertX" : "Vert") +
                    " necesita rampas nativas (slots 1/2): esta instalacion "
                    "solo ofrece el repuesto analitico" + std::string(detail)};
            }
            return {{}, "No se encontro ni glow/gradiente nativo ni repuesto para el modo " +
                std::string(options.mode == ImportMode::Blur ? "Blur"
                    : options.mode == ImportMode::VertX ? "VertX" : "Vert") +
                ": faltan " + missing + detail};
        }
    }
    int frameLimit = std::min(options.maxFrames, static_cast<int>(source.frames.size()));
    Options searchOptions = options;
    searchOptions.motion = false;
    PreviewThrottle throttle{std::move(preview)};
    // no callback means nobody to notify: pass null and skip the raster.
    PreviewThrottle* previewPtr = throttle.callback ? &throttle : nullptr;
    auto result = options.mode == ImportMode::Render
        ? buildRenderPlan(source, searchOptions, frameLimit, progress, previewPtr)
        : buildRegularPlan(source, searchOptions, frameLimit, progress, previewPtr);
    if (options.motion) result = tryMotionPlan(source, options, std::move(result));
    // glow last: the halo must not lower the grid.
    if (result && !usesSoftGeometry(options.mode)) {
        applyGlow(
            result.plan, options.glow, static_cast<std::size_t>(options.objectBudget));
    }
    return result;
}

} // namespace paimon::gifimport
