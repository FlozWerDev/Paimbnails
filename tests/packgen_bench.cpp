// Bench de PackGen v2: MPix/s del kernel de tinte y del packer.
//
// Compila sin Geode (nucleo puro):
//   g++ -std=c++17 -O2 -pthread -o /tmp/pgbench tests/packgen_bench.cpp && /tmp/pgbench
//
// No falla nunca (devuelve 0): imprime numeros para comparar antes/despues.
// Pasale un multiplicador opcional de carga: /tmp/pgbench [1].

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "../src/features/texture-studio/packgen/FrameImage.hpp"
#include "../src/features/texture-studio/packgen/MaxRectsPacker.hpp"
#include "../src/features/texture-studio/packgen/PackScheduler.hpp"
#include "../src/features/texture-studio/packgen/SelfCheck.hpp"
#include "../src/features/texture-studio/packgen/TintEngine.hpp"

using namespace paimon::texture_studio::packgen;

namespace {

double secondsSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();
}

// Sprite rugoso determinista: barre el RGB para que la luminancia varie.
void fillRough(FrameImage& img, std::vector<std::uint8_t>& mask) {
    int W = img.width(), H = img.height();
    mask.assign(static_cast<std::size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::uint8_t v = static_cast<std::uint8_t>((x * 31 + y * 57) & 0xFF);
            img.setAt(x, y, {v, static_cast<std::uint8_t>(255 - v),
                             static_cast<std::uint8_t>((v * 3) & 0xFF), 255});
            mask[static_cast<std::size_t>(y) * W + x] =
                (v > 8) ? 255 : 0;  // ~97% cobertura, como un sprite real
        }
    }
}

void benchTint(int W, int H, int iters) {
    FrameImage src(W, H), dst(W, H);
    std::vector<std::uint8_t> mC1, mC2;
    fillRough(src, mC1);
    mC2 = mC1;
    dst.blitOverwrite(0, 0, src);

    AlphaLut lut = AlphaLut::make();
    PrecomputedTint c1 = PrecomputedTint::make(255, 64, 64, 160.0f, 1.1f, 0.05f);
    PrecomputedTint c2 = PrecomputedTint::make(64, 64, 255, 160.0f, 0.9f, -0.05f);

    // Calentar caches antes de medir.
    tintStackImage(src.data(), dst.data(), W, H, mC1.data(), mC2.data(),
                   nullptr, nullptr, c1, c2, c1, c2, false, false, 0, lut);

    auto t0 = std::chrono::steady_clock::now();
    std::size_t counted = 0;
    for (int i = 0; i < iters; ++i) {
        counted += tintStackImage(src.data(), dst.data(), W, H,
                                  mC1.data(), mC2.data(), nullptr, nullptr,
                                  c1, c2, c1, c2, false, false, 0, lut);
    }
    double s = secondsSince(t0);
    double mpix = static_cast<double>(W) * H * iters / 1e6;
    std::printf("tint %dx%d x%d: %.1f MPix/s (%.3f s, %zu px contados)\n",
                W, H, iters, mpix / s, s, counted / static_cast<std::size_t>(iters));
}

void benchTintParallel(int W, int H, int iters) {
    FrameImage src(W, H);
    std::vector<std::uint8_t> mC1;
    fillRough(src, mC1);

    // Un FrameImage por hilo (el kernel escribe su propio dst).
    PackScheduler pool;
    std::size_t T = pool.threadCount();
    std::vector<FrameImage> dsts;
    for (std::size_t t = 0; t < T; ++t) {
        dsts.emplace_back(W, H);
        dsts.back().blitOverwrite(0, 0, src);
    }
    AlphaLut lut = AlphaLut::make();
    PrecomputedTint c1 = PrecomputedTint::make(255, 64, 64, 160.0f, 1.0f, 0.0f);
    PrecomputedTint c2 = PrecomputedTint::make(64, 64, 255, 160.0f, 1.0f, 0.0f);

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iters; ++i) {
        pool.parallelFor<std::size_t>(0, T, [&](std::size_t t) {
            tintStackImage(src.data(), dsts[t].data(), W, H,
                           mC1.data(), mC1.data(), nullptr, nullptr,
                           c1, c2, c1, c2, false, false, 0, lut);
        });
    }
    double s = secondsSince(t0);
    double mpix = static_cast<double>(W) * H * iters * T / 1e6;
    std::printf("tint-paralelo %dx%d x%d x%zu hilos: %.1f MPix/s (%.3f s)\n",
                W, H, iters, T, mpix / s, s);
}

void benchPacker(int n) {
    std::vector<PackRect> rects;
    for (int i = 0; i < n; ++i)
        rects.push_back({16 + (i * 37) % 240, 16 + (i * 53) % 240, i});

    constexpr int kIters = 20;
    auto t0 = std::chrono::steady_clock::now();
    MaxRectsPacker::Result last;
    for (int i = 0; i < kIters; ++i) last = MaxRectsPacker().pack(rects);
    double s = secondsSince(t0);
    std::printf("packer %d rects x%d: %.2f ms/pack (atlas %dx%d)\n",
                n, kIters, 1000.0 * s / kIters, last.atlasW, last.atlasH);
}

}  // namespace

int main(int argc, char** argv) {
    int load = (argc > 1) ? std::atoi(argv[1]) : 1;
    if (load < 1) load = 1;

    auto core = runSelfCheck();
    std::printf("selfcheck: %s\n", core.ok ? "OK" : core.failedStep.c_str());

    benchTint(512, 512, 10 * load);
    benchTint(2048, 2048, 2 * load);
    benchTintParallel(512, 512, 10 * load);
    benchPacker(64 * load);
    benchPacker(512 * load);
    return 0;
}
