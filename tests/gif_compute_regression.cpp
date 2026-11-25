// Real CPU/GPU parity and timing; runs without the game or an OpenGL context.
#define main gifImportRegressionMain
#include "gif_import_regression.cpp"
#undef main

#include <chrono>
#include <bit>

std::uint64_t quantizationEdgeCases() {
    std::uint64_t hash = 14695981039346656037ull;
    // Odd byte counts exercise raw-mask padding. The larger source selects
    // analysis stride 2, while the odd output exercises partial thread groups.
    for (auto [width, height] : {std::pair{277, 195}, {2113, 2001}}) {
        auto source = animation(width, height, 1);
        auto& rgba = source.frames.front().rgba;
        for (std::size_t pixel = 0; pixel < rgba.size() / 4; ++pixel) {
            rgba[pixel * 4] = static_cast<std::uint8_t>(pixel * 13);
            rgba[pixel * 4 + 1] = static_cast<std::uint8_t>(pixel * 7);
            rgba[pixel * 4 + 2] = static_cast<std::uint8_t>(pixel * 3);
            rgba[pixel * 4 + 3] = static_cast<std::uint8_t>(pixel % 5 ? 255 : 95);
        }
        auto options = exactOptions(193);
        auto const selected = selectFrames(source, 1);
        auto masks = backgroundMasks(source, selected, options);
        for (std::size_t pixel = 0; pixel < masks.front().size(); ++pixel)
            masks.front()[pixel] = pixel % 11 == 0;
        std::vector<Color> palette;
        for (int index = 0; index < 64; ++index) palette.push_back({
            static_cast<std::uint8_t>(index * 7), static_cast<std::uint8_t>(index * 11),
            static_cast<std::uint8_t>(index * 13)});
        for (int threshold : {1, 96, 254}) {
            options.alphaThreshold = threshold;
            auto const frames = quantizeFromSource(source, selected, masks, palette, 193, 129, options);
            for (auto const& frame : frames) for (auto cell : frame.cells) {
                hash ^= static_cast<std::uint32_t>(cell);
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

int main() {
    auto source = animation(640, 360, 12);
    for (std::size_t frame = 0; frame < source.frames.size(); ++frame) {
        auto& rgba = source.frames[frame].rgba;
        for (int y = 0; y < source.height; ++y) for (int x = 0; x < source.width; ++x) {
            auto const offset = (static_cast<std::size_t>(y) * source.width + x) * 4;
            rgba[offset] = static_cast<std::uint8_t>((x * 13 + y * 7 + frame * 19) % 256);
            rgba[offset + 1] = static_cast<std::uint8_t>((x * 3 + y * 11) % 256);
            rgba[offset + 2] = static_cast<std::uint8_t>((x + y + frame * 5) % 256);
            rgba[offset + 3] = static_cast<std::uint8_t>((x + y) % 7 ? 255 : 0);
        }
    }
    auto options = exactOptions(256);
    auto const selected = selectFrames(source, 12);
    auto const masks = backgroundMasks(source, selected, options);
    std::vector<Color> palette;
    for (int i = 0; i < 32; ++i) palette.push_back({
        static_cast<std::uint8_t>(i * 7), static_cast<std::uint8_t>(255 - i * 5),
        static_cast<std::uint8_t>(i * 3)});
    auto const start = std::chrono::steady_clock::now();
    auto const frames = quantizeFromSource(source, selected, masks, palette, 256, 144, options);
    auto const elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::uint64_t hash = 14695981039346656037ull;
    for (auto const& frame : frames) for (auto cell : frame.cells) {
        hash ^= static_cast<std::uint32_t>(cell);
        hash *= 1099511628211ull;
    }
    std::cout << "quantize-ms=" << elapsed << " frames=" << frames.size() << " hash=" << hash << '\n';
    auto const warmStart = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 4; ++repeat) {
        auto const warm = quantizeFromSource(source, selected, masks, palette, 256, 144, options);
        for (std::size_t index = 0; index < frames.size(); ++index)
            if (warm[index].cells != frames[index].cells) return 1;
    }
    std::cout << "warm-quantize-4-ms=" << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - warmStart).count() << '\n';
    std::cout << "quantize-masks-alpha-stride-64-colors hash=" << quantizationEdgeCases() << '\n';
    std::vector<std::uint8_t> pixels(320 * 320 * 4, 255);
    SurfaceImage image{320, 320, {}, {}, {}, pixels};
    surfaceopt::Composite composite(image);
    std::vector<Primitive> shapes;
    for (int i = 0; i < 120; ++i) {
        shapes.push_back({static_cast<float>((i * 37) % 320), static_cast<float>((i * 71) % 320),
            10.f + i % 60, 3.f + i % 20, static_cast<float>(i * 17 % 360), 0,
            static_cast<PrimitiveKind>(i % 5)});
        shapes.back().skew = shapes.back().kind == PrimitiveKind::Circle ? 0.f : static_cast<float>(i % 30);
    }
    auto const coverageStart = std::chrono::steady_clock::now();
    auto const expected = composite.measureOnCpu(shapes);
    for (int repeat = 0; repeat < 40; ++repeat) composite.measureOnCpu(shapes);
    std::cout << "cpu-coverage-40-ms=" << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - coverageStart).count() << '\n';
#if defined(PAIM_GIF_COMPUTE)
    std::vector<std::uint32_t> computed;
    if (!computeSurfaceCoverage(xformsOf(shapes), 320, 320, false, computed)) {
        std::cerr << "FAIL: hardware GPU coverage unavailable\n";
        return 1;
    }
    for (std::size_t cell = 0; cell < computed.size(); ++cell) {
        if ((computed[cell] & 31) != expected.counts[cell] ||
            ((computed[cell] >> 5) & 31) != expected.opaqueCounts[cell]) {
            std::cerr << "FAIL: GPU coverage differs at cell " << cell << " gpu=" << (computed[cell] & 31)
                << '/' << ((computed[cell] >> 5) & 31) << " cpu=" << static_cast<int>(expected.counts[cell])
                << '/' << static_cast<int>(expected.opaqueCounts[cell]) << '\n';
            return 1;
        }
    }
    auto const gpuStart = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 40; ++repeat)
        if (!computeSurfaceCoverage(xformsOf(shapes), 320, 320, false, computed)) return 1;
    std::cout << "gpu-coverage-40-ms=" << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - gpuStart).count() << " parity=PASS\n";

    for (auto const [width, height] : {std::pair{128, 128}, {193, 129}, {320, 320}, {512, 288}}) {
        std::vector<std::uint8_t> transparent(static_cast<std::size_t>(width) * height * 4, 100);
        SurfaceImage const transparentImage{width, height, {}, {}, {}, transparent};
        surfaceopt::Composite const transparentComposite(transparentImage);
        auto const reference = transparentComposite.measureOnCpu(shapes);
        if (!computeSurfaceCoverage(xformsOf(shapes), width, height, true, computed)) return 1;
        for (std::size_t cell = 0; cell < computed.size(); ++cell) {
            std::uint32_t const cpu = reference.counts[cell] | (reference.opaqueCounts[cell] << 5) |
                (static_cast<std::uint32_t>(reference.centers[cell]) << 10);
            if (computed[cell] != cpu) {
                std::cerr << "FAIL: transparent GPU coverage " << width << 'x' << height << " cell=" << cell
                    << " gpu=" << computed[cell] << " cpu=" << cpu << '\n';
                return 1;
            }
        }
    }
    std::cout << "transparent-rotated-skewed-coverage: PASS\n";
    std::vector<Primitive> const unsupported{{20.f, 20.f, 5.f, 5.f, 0.f, 0, PrimitiveKind::Stamp}};
    if (computeSurfaceCoverage(xformsOf(unsupported), 320, 320, false, computed) ||
        computeSurfaceCoverage(xformsOf(shapes), 0, 320, false, computed) ||
        computeSurfaceCoverage(xformsOf(shapes), 32, 32, false, computed)) return 1;
    std::cout << "unsupported-and-small-workloads-fallback: PASS\n";
    std::atomic<bool> concurrent{true};
    parallelFor(8, [&](std::size_t) {
        std::vector<std::uint32_t> local;
        if (parallelThreads(100) != 1 || workerLimit() > 4 ||
            !computeSurfaceCoverage(xformsOf(shapes), 320, 320, false, local)) {
            concurrent.store(false);
            return;
        }
        for (std::size_t cell = 0; cell < local.size(); ++cell)
            if ((local[cell] & 31) != expected.counts[cell]) concurrent.store(false);
    });
    if (!concurrent.load()) return 1;
    std::cout << "concurrent-imports-and-bounded-workers: PASS\n";
#endif

    auto drawing = animation(192, 192, 1, 0, 0, 0, 0);
    for (int y = 0; y < 192; ++y) for (int x = 0; x < 192; ++x) {
        float const dx = x - 96.f, dy = y - 96.f;
        if (dx * dx + dy * dy < 85.f * 85.f) {
            bool const stripe = y < x / 2 + 50;
            setPixel(drawing, 0, x, y, stripe ? 245 : 25, stripe ? 180 : 75, stripe ? 95 : 150);
        }
    }
    auto paint = exactOptions(192);
    paint.mode = ImportMode::Paint;
    paint.sampling = SamplingMode::Smooth;
    auto const planStart = std::chrono::steady_clock::now();
    auto const result = buildPlan(drawing, paint);
    if (!result) { std::cerr << result.error << '\n'; return 1; }
    std::uint64_t planHash = 14695981039346656037ull;
    auto append = [&](std::uint32_t value) { planHash ^= value; planHash *= 1099511628211ull; };
    for (auto color : result.plan.palette) { append(color.r); append(color.g); append(color.b); }
    for (auto const& object : result.plan.staticObjects) {
        for (auto value : {object.x, object.y, object.width, object.height, object.rotation, object.skew})
            append(std::bit_cast<std::uint32_t>(value));
        append(object.color); append(static_cast<std::uint32_t>(object.kind)); append(object.layer);
    }
    std::cout << "paint-plan-ms=" << std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - planStart).count() << " objects=" << result.plan.totalObjects
        << " hash=" << planHash << '\n';
    return frames.size() == 12 ? 0 : 1;
}
