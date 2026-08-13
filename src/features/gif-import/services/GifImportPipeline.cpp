#include "GifImportPipeline.hpp"
#include "GifArtVectorizer.hpp"
#include "GifPaintVectorizer.hpp"
#include "ImageWatermark.hpp"

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

// Una imagen fija puede pagar el detalle de 320 px; una animacion multiplica ese
// costo por frame, asi que se queda donde el trazado tarda segundos y no minutos.
Options sanitize(Options options, std::size_t frames) {
    options.maxDimension = std::clamp(options.maxDimension, 4, frames > 1 ? 160 : 320);
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

// El dibujo esta hecho de manchas planas, pero el antialias deja pegada a cada
// borde una rampa de tonos intermedios que no es ningun color del dibujo. Si esa
// rampa entra en la paleta se lleva la mitad de las entradas, deja los colores de
// verdad mal representados y ademas cada tono de la rampa es una hebra de una
// celda de ancho que cuesta un objeto por cada dos celdas. Contar cuantos vecinos
// llevan el mismo color separa las dos cosas sin mirar el color en si: el interior
// de una mancha plana los tiene todos y la orla no tiene ninguno.
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
    // Se parte la caja que peor representa a los colores que lleva dentro, medido
    // como lo que se desvia cada uno de la media de la caja. Repartir por peso
    // partia en dos la mancha mas grande aunque ya fuera de un solo color, y de
    // ahi salian paletas con cuatro azules identicos y ningun hueco para el
    // detalle pequeno.
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
            // Una caja que no llega ni al milesimo de la imagen no merece una
            // entrada suya: seria gastarla en una mota.
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

// El histograma de la imagen reducida. En modo Pintura cada celda pesa ademas
// por lo plana que es su vecindad, para que la paleta se la lleven los colores
// del dibujo y no la orla del antialias.
std::vector<Color> buildPalette(
    std::vector<ReducedFrame> const& frames,
    int maxColors,
    int width,
    int height,
    bool flat
) {
    auto histogram = emptyHistogram();
    for (auto const& frame : frames) {
        auto const delay = static_cast<std::uint64_t>(std::clamp(frame.delayMs, 10, 1000));
        auto sample = [&](int x, int y, Color& out) {
            auto const& pixel = frame.pixels[static_cast<std::size_t>(y) * width + x];
            if (pixel.a == 0) return false;
            out = {pixel.r, pixel.g, pixel.b};
            return true;
        };
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                Color color;
                if (!sample(x, y, color)) continue;
                addSample(histogram, color, delay * (flat
                    ? flatnessWeight(sample, x, y, width, height) : 1));
            }
        }
    }
    return medianCut(histogram, maxColors);
}

constexpr int kSpeckleColorDistance = 50;
constexpr int kSmallPaletteSpeckleDistance = 65;

// Una celda suelta de un color no dibuja nada: a la escala a la que se ve el
// nivel es un punto, pero cuesta un objeto entero y ademas rompe en dos la mancha
// del vecino, que tiene que rodearla. Se funde con el color que mas la rodea sin
// mirar si se parece, porque a este tamano no hay detalle que perder. Solo caen
// las que estan rodeadas del todo: una mota pegada al borde del dibujo si se ve.
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
                // Una mota flotando sola en el vacio no la presencia nadie: a la
                // escala a la que se ve el nivel es un punto suelto, y ademas es la
                // trama de puntos del fondo del dibujo, que no es ningun detalle.
                // Se borra entera en vez de fundirla, que no hay con que fundirla.
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

// El vecino mas pegado a la mancha, siempre que sea un color casi igual: una mota
// de ese tamano no dibuja nada que el vecino no dibuje.
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

// La orla que deja el antialias es una hebra cuyo color cae justo en la recta
// entre los dos colores que separa, porque es la mezcla de ambos. Un detalle de
// verdad, una linea de un pixel, no cumple eso: su color se sale de la recta. Solo
// cuando la mezcla se confirma la hebra se va al mas parecido de los dos lados,
// que es lo que haria un dibujante a mano y de paso deja de costar un objeto por
// pixel. La distancia va sin elevar al cuadrado porque lo que se compara es si un
// lado mas el otro suman lo que mide el salto entero.
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
        // El antialias no deja una hebra sino una rampa de varias, una encima de
        // otra, asi que hacen falta varias pasadas: cada una se come la de fuera y
        // deja al descubierto la siguiente.
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
                // Una hebra es una mancha de una o dos celdas de ancho: no tiene
                // ninguna celda rodeada de su propio color por los cuatro lados.
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
            bridged[static_cast<std::size_t>(position)] =
                static_cast<std::int32_t>(replacement);
        }
        frame.cells = std::move(bridged);
    }
}

struct GeometryContext {
    ImportMode mode = ImportMode::Blocks;
    std::vector<std::vector<std::uint8_t>> obstacles;
    std::vector<int> ranks;
    std::vector<std::uint8_t> empty;
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
        case ImportMode::Render:
            return vectorizePaint(
                positions, width, height, color,
                context.ranks[static_cast<std::size_t>(color)],
                context.obstacles[static_cast<std::size_t>(color)],
                context.empty);
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
    int height
) {
    auto repairs = paintSeamRepairs(objects, cells, ranks, width, height);
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

// Celda que ningun frame pinta. Una figura puede asomar ahi sin ensuciar nada,
// que es lo que deja rematar en diagonal los detalles sueltos sin dejar picos
// sobre otro color.
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
    if (usesPaintGeometry(context.mode)) {
        prunePaintObjects(candidate.staticObjects, width, height);
    }
    for (auto& track : candidate.tracks) {
        sortByLayer(track.objects);
        if (usesPaintGeometry(context.mode)) {
            prunePaintObjects(track.objects, width, height);
        }
    }
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
    if (usesPaintGeometry(context.mode)) {
        prunePaintObjects(candidate.staticObjects, width, height);
    }

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
        if (usesPaintGeometry(context.mode)) {
            prunePaintObjects(track.objects, width, height);
        }
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
    StageProgress const& progress = {}
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
    auto reduced = reduceFrames(source, selected, width, height, options);
    report(progress, BuildStage::Resizing, 0.28f);
    auto palette = buildPalette(
        reduced, options.maxColors, width, height,
        usesPaintGeometry(options.mode));
    if (palette.empty()) return {{}, "El GIF quedo completamente transparente con estos ajustes."};
    report(progress, BuildStage::Palette, 0.4f);
    auto frames = quantize(reduced, palette, width, height, options.dither);
    if (frames.empty()) return {{}, "No quedaron frames validos despues de procesar el GIF."};
    report(progress, BuildStage::Geometry, 0.5f);
    // Geometry uses the untouched grid; displayed fidelity uses the source pixels.
    auto const referenceFrames = frames;
    if (usesPaintGeometry(options.mode)) {
        // Una mota es lo que no llega a la cuatromilesima parte del dibujo. Con el
        // umbral mas alto se ahorraban objetos, pero en un dibujo hecho a pixel el
        // detalle chico esta puesto a proposito y se lo llevaba por delante. A poca
        // resolucion no llega ni a una celda y no se toca nada.
        dissolveSpecks(
            frames, palette, width, height, width * height / 4000);
        if (compactSpeckles) {
            compactPaintSpeckles(frames, reduced, palette, width, height);
        }
    }
    GeometryContext context;
    context.mode = options.mode;
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
        for (int color = 0; color < static_cast<int>(palette.size()); ++color) {
            auto objects = buildGeometry(
                positions[static_cast<std::size_t>(color)], width, height, color, context);
            chosen.staticObjects.insert(
                chosen.staticObjects.end(), objects.begin(), objects.end());
        }
        sortByLayer(chosen.staticObjects);
        if (usesPaintGeometry(context.mode)) {
            prunePaintObjects(chosen.staticObjects, width, height);
            repairPaintSeams(
                chosen.staticObjects, frames.front().cells, context.ranks, width, height);
            prunePaintObjects(chosen.staticObjects, width, height);
            repairPaintSeams(
                chosen.staticObjects, frames.front().cells, context.ranks, width, height);
        }
    } else {
        auto temporal = temporalCandidate(frames, width, height, options.loop, context);
        auto perFrame = frameCandidate(
            frames, width, height, static_cast<int>(palette.size()), options.loop, context);
        chosen = chooseCandidate(std::move(temporal), std::move(perFrame), options.objectBudget);
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
    plan.strategy = std::move(chosen.strategy);
    applyImageWatermark(plan, options.objectBudget);
    plan.visualObjects = plan.staticObjects.size();
    for (auto const& track : plan.tracks) plan.visualObjects += track.objects.size();
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
    if (usesPaintGeometry(plan.mode)) {
        report(progress, BuildStage::Reviewing, 0.9f);
        plan.geometrySimilarity = paintPlanSimilarity(plan, referenceFrames, {});
        plan.similarity = paintPlanSimilarity(plan, referenceFrames, reduced);
        plan.detailSimilarity = plan.similarity;
        if (plan.mode == ImportMode::Render && plan.frames.size() == selected.size()) {
            auto detailed = reduceFrames(
                source, selected, width * 2, height * 2, options);
            plan.detailSimilarity = sourcePlanSimilarity(plan, detailed, 2);
        }
    }
    report(progress, BuildStage::Reviewing, 1.f);
    return {std::move(plan), {}};
}

bool planFits(ImportPlan const& plan, Options const& options) {
    return plan.totalObjects <= static_cast<std::size_t>(options.objectBudget) &&
        plan.triggerObjects <= kPlaybackTriggerLimit &&
        plan.tracks.size() + animationEventGroupCount(plan.frames.size(), options.loop) < 9800;
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
    BuildProgressCallback const& progress
) {
    auto const dimensions = renderDimensions(options);
    int const passes = static_cast<int>(dimensions.size());
    std::size_t const softLimit = std::min<std::size_t>(
        options.objectBudget, source.frames.size() > 1 ? 6000 : 2500);
    ImportPlan best;
    bool hasBest = false;
    int attempted = 0;

    for (int index = 0; index < passes; ++index) {
        float const start = 0.01f + 0.97f * index / passes;
        float const length = 0.97f / passes;
        auto compactProgress = progressRange(
            progress, start, length * 0.7f, index + 1, passes);
        auto result = buildAt(
            source, options, dimensions[static_cast<std::size_t>(index)],
            frameLimit, true, compactProgress);
        ++attempted;
        if (!result) {
            finishProgress(progress, attempted, passes);
            return result;
        }
        result.plan.requestedDimension = options.maxDimension;

        if (result.plan.geometrySimilarity < kPaintReviewGate) {
            auto plainProgress = progressRange(
                progress, start + length * 0.7f, length * 0.25f,
                index + 1, passes);
            auto plain = buildAt(
                source, options, dimensions[static_cast<std::size_t>(index)],
                frameLimit, false, plainProgress);
            if (plain && plain.plan.geometrySimilarity > result.plan.geometrySimilarity) {
                result = std::move(plain);
                result.plan.requestedDimension = options.maxDimension;
            }
        }

        if (progress) {
            progress({BuildStage::Refining, start + length * 0.98f, index + 1, passes});
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
    BuildProgressCallback const& progress
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
            source, options, dimension, frameLimit, true, compactProgress);
        if (!result) {
            finishProgress(progress);
            return result;
        }
        result.plan.requestedDimension = options.maxDimension;
        if (usesPaintGeometry(options.mode) &&
            result.plan.geometrySimilarity < kPaintReviewGate) {
            auto plainProgress = progressRange(
                progress, start + length * 0.68f, length * 0.3f);
            auto plain = buildAt(
                source, options, dimension, frameLimit, false, plainProgress);
            if (plain && plain.plan.geometrySimilarity > result.plan.geometrySimilarity) {
                result = std::move(plain);
                result.plan.requestedDimension = options.maxDimension;
            }
        }
        if (planFits(result.plan, options)) {
            if (!usesPaintGeometry(options.mode) ||
                result.plan.geometrySimilarity >= kPaintReviewGate) {
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

} // namespace

BuildResult buildPlan(
    SourceAnimation const& source,
    Options const& rawOptions,
    BuildProgressCallback progress
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
    int frameLimit = std::min(options.maxFrames, static_cast<int>(source.frames.size()));
    if (options.mode == ImportMode::Render) {
        return buildRenderPlan(source, options, frameLimit, progress);
    }
    return buildRegularPlan(source, options, frameLimit, progress);
}

} // namespace paimon::gifimport
