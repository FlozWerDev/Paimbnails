// Tests de pintado de PackGen v2: el kernel de tinte contra TintMath.
//
// Compila sin Geode (nucleo puro + stub de ccTypes):
//   g++ -std=c++17 -O2 -I tests/packgen_stubs -o /tmp/pgpaint tests/packgen_paint_regression.cpp && /tmp/pgpaint
//
// Cada prueba es una funcion bool. main() las ejecuta todas y devuelve 0 si
// pasan o 1 si alguna falla.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../src/features/texture-studio/packgen/ContentHash.hpp"
#include "../src/features/texture-studio/packgen/FrameImage.hpp"
#include "../src/features/texture-studio/packgen/TintEngine.hpp"
#include "../src/features/texture-studio/engine/TintMath.hpp"

using namespace paimon::texture_studio::packgen;
using namespace paimon::texture_studio::tintmath;

namespace {

// LCG determinista: los mismos "aleatorios" en cada run y plataforma.
struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed) {}
    std::uint32_t next() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(s >> 33);
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(next() & 0xFF); }
    float range(float lo, float hi) {
        return lo + (hi - lo) * (static_cast<float>(next()) / 4294967296.0f);
    }
};

int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #cond "\n"; \
        return false; \
    } \
} while (0)

// Mismos numeros que el SelfTest del motor: gris 200 con brillo 160
// (factor 1.25) a traves de tinte (160,80,40) da (200,100,50).
bool tint_scale_known_value() {
    PrecomputedTint spec = PrecomputedTint::make(160, 80, 40, 160.0f, 1.0f, 0.0f);
    std::uint8_t r, g, b;
    tintPixelFast(200, 200, 200, spec, r, g, b);
    CHECK(r == 200 && g == 100 && b == 50);

    std::uint8_t er, eg, eb;
    tintByLuminance(200, 200, 200, cocos2d::ccColor3B(160, 80, 40),
                    160.0f, 1.0f, 0.0f, er, eg, eb);
    CHECK(er == 200 && eg == 100 && eb == 50);
    CHECK(r == er && g == eg && b == eb);
    return true;
}

bool tint_red_clamps() {
    PrecomputedTint spec = PrecomputedTint::make(255, 0, 0, 160.0f, 1.0f, 0.0f);
    std::uint8_t r, g, b;
    tintPixelFast(200, 200, 200, spec, r, g, b);
    CHECK(r == 255 && g == 0 && b == 0);

    // Negro puro: luminancia 0 -> factor 0 -> negro.
    tintPixelFast(0, 0, 0, spec, r, g, b);
    CHECK(r == 0 && g == 0 && b == 0);
    return true;
}

// Sin mascaras el kernel no toca nada (passthrough de orla/fondo).
bool outline_passthrough() {
    constexpr int W = 8, H = 8;
    FrameImage src(W, H), dst(W, H);
    Rng rng(7);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            src.setAt(x, y, {rng.byte(), rng.byte(), rng.byte(), 255});
    dst.blitOverwrite(0, 0, src);

    AlphaLut lut = AlphaLut::make();
    PrecomputedTint c = PrecomputedTint::make(255, 0, 0, 160.0f, 1.0f, 0.0f);
    std::size_t n = tintStackImage(src.data(), dst.data(), W, H,
                                   nullptr, nullptr, nullptr, nullptr,
                                   c, c, c, c, false, false, 0, lut);
    CHECK(n == 0);
    CHECK(dst == src);
    return true;
}

// Pixeles transparentes y bajo el umbral oscuro se saltan sin contar.
bool transparent_and_dark_skipped() {
    constexpr int W = 4, H = 2;
    FrameImage src(W, H), dst(W, H);
    src.setAt(0, 0, {200, 200, 200, 0});    // transparente
    src.setAt(1, 0, {200, 200, 200, 255});  // visible
    src.setAt(2, 0, {10, 10, 10, 255});     // oscuro
    src.setAt(3, 0, {200, 200, 200, 255});  // visible
    for (int x = 0; x < W; ++x)
        src.setAt(x, 1, {200, 200, 200, 255});
    dst.blitOverwrite(0, 0, src);

    std::vector<std::uint8_t> mask(W * H, 255);
    AlphaLut lut = AlphaLut::make();
    PrecomputedTint c = PrecomputedTint::make(160, 80, 40, 160.0f, 1.0f, 0.0f);
    std::size_t n = tintStackImage(src.data(), dst.data(), W, H,
                                   mask.data(), nullptr, nullptr, nullptr,
                                   c, c, c, c, false, false, 64, lut);
    // (0,0) transparente + (2,0) oscuro se saltan: 6 de 8.
    CHECK(n == 6);
    auto t = dst.at(0, 0);
    CHECK(t.a == 0);
    auto d = dst.at(2, 0);
    CHECK(d.r == 10 && d.g == 10 && d.b == 10);
    auto v = dst.at(1, 0);
    CHECK(v.r == 200 && v.g == 100 && v.b == 50);
    return true;
}

// Circulo sintetico: interior tintado, esquinas intactas, particion C1/C2.
bool circle_coverage_and_partition() {
    constexpr int W = 32, H = 32, CX = 16, CY = 16, R = 12;
    FrameImage src(W, H), dst(W, H);
    std::vector<std::uint8_t> mC1(W * H, 0), mC2(W * H, 0);
    int ink = 0, left = 0, right = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int dx = x - CX, dy = y - CY;
            if (dx * dx + dy * dy <= R * R) {
                src.setAt(x, y, {0x3F, 0xCC, 0x2F, 255});
                if (x < CX) { mC1[static_cast<std::size_t>(y) * W + x] = 255; ++left; }
                else { mC2[static_cast<std::size_t>(y) * W + x] = 255; ++right; }
                ++ink;
            }
        }
    }
    dst.blitOverwrite(0, 0, src);

    AlphaLut lut = AlphaLut::make();
    PrecomputedTint c1 = PrecomputedTint::make(255, 64, 64, 160.0f, 1.0f, 0.0f);
    PrecomputedTint c2 = PrecomputedTint::make(64, 64, 255, 160.0f, 1.0f, 0.0f);
    std::size_t n = tintStackImage(src.data(), dst.data(), W, H,
                                   mC1.data(), mC2.data(), nullptr, nullptr,
                                   c1, c2, c1, c2, false, false, 0, lut);
    CHECK(n == static_cast<std::size_t>(ink));

    // Particion: cada pixel de tinta contado exactamente una vez.
    CHECK(left + right == ink);
    CHECK(left > 0 && right > 0);

    // El centro-izquierda debe ser rojo-dominante, el centro-derecha azul.
    auto l = dst.at(CX - 4, CY);
    CHECK(l.r > l.g && l.r > l.b);
    auto r = dst.at(CX + 4, CY);
    CHECK(r.b > r.r && r.b > r.g);

    // Esquinas fuera del circulo: transparentes e intactas.
    for (auto [x, y] : {std::pair<int,int>{0,0}, {W-1,0}, {0,H-1}, {W-1,H-1}}) {
        auto p = dst.at(x, y);
        CHECK(p.a == 0 && p.r == 0 && p.g == 0 && p.b == 0);
    }
    return true;
}

// Diagonal fina y larga (Bresenham): sin huecos, vecinos intactos.
bool thin_diagonal_no_gaps() {
    constexpr int W = 128, H = 16;
    FrameImage src(W, H), dst(W, H);
    std::vector<std::uint8_t> mask(W * H, 0);
    // Bresenham de (0,0) a (W-1,H-1).
    int dx = W - 1, dy = H - 1, err = dx - dy, x = 0, y = 0, lineLen = 0;
    while (true) {
        src.setAt(x, y, {180, 180, 180, 255});
        mask[static_cast<std::size_t>(y) * W + x] = 255;
        ++lineLen;
        if (x == W - 1 && y == H - 1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; ++x; }
        if (e2 < dx) { err += dx; ++y; }
    }
    dst.blitOverwrite(0, 0, src);

    AlphaLut lut = AlphaLut::make();
    PrecomputedTint c = PrecomputedTint::make(160, 80, 40, 160.0f, 1.0f, 0.0f);
    std::size_t n = tintStackImage(src.data(), dst.data(), W, H,
                                   mask.data(), nullptr, nullptr, nullptr,
                                   c, c, c, c, false, false, 0, lut);
    CHECK(n == static_cast<std::size_t>(lineLen));

    // Sin escalera visible como hueco: cada columna con tinta tiene el pixel
    // tintado (no se quedo con el color original).
    for (int cx = 0; cx < W; ++cx) {
        bool found = false;
        for (int cy = 0; cy < H; ++cy) {
            if (mask[static_cast<std::size_t>(cy) * W + cx]) {
                auto p = dst.at(cx, cy);
                if (p.r != 180 || p.g != 180 || p.b != 180) found = true;
            }
        }
        CHECK(found);
    }
    // Vecinos fuera de la mascara intactos (transparentes).
    CHECK(dst.at(0, H - 1).a == 0);
    CHECK(dst.at(W - 1, 0).a == 0);
    return true;
}

// Diferencial aleatorio kernel vs TintMath (mismas formulas, otro camino).
// Rango restringido al dominio con clamp (brillo [1,1000], sat [0,3],
// contraste [-1,1]) donde ambos deben coincidir bit a bit.
bool randomized_tint_differential() {
    Rng rng(123456789ULL);
    AlphaLut lut = AlphaLut::make();
    for (int i = 0; i < 4000; ++i) {
        std::uint8_t sr = rng.byte(), sg = rng.byte(), sb = rng.byte();
        std::uint8_t tr = rng.byte(), tg = rng.byte(), tb = rng.byte();
        float bright = rng.range(1.0f, 1000.0f);
        float sat = rng.range(0.0f, 3.0f);
        float con = rng.range(-1.0f, 1.0f);

        PrecomputedTint spec = PrecomputedTint::make(tr, tg, tb, bright, sat, con);
        std::uint8_t kr, kg, kb;
        tintPixelFast(sr, sg, sb, spec, kr, kg, kb);

        std::uint8_t er, eg, eb;
        tintByLuminance(sr, sg, sb, cocos2d::ccColor3B(tr, tg, tb),
                        bright, sat, con, er, eg, eb);
        if (kr != er || kg != eg || kb != eb) {
            std::cout << "FAIL randomized_tint_differential iter " << i
                      << ": kernel=(" << (int)kr << "," << (int)kg << "," << (int)kb
                      << ") ref=(" << (int)er << "," << (int)eg << "," << (int)eb << ")\n";
            return false;
        }
    }
    // El LUT guarda exactamente a/255.0f.
    for (int a = 0; a < 256; ++a) {
        if (lut.v[a] != static_cast<float>(a) / 255.0f) {
            std::cout << "FAIL alpha lut entry " << a << "\n";
            return false;
        }
    }
    return true;
}

bool randomized_blend_differential() {
    Rng rng(987654321ULL);
    AlphaLut lut = AlphaLut::make();
    for (int i = 0; i < 4000; ++i) {
        std::uint8_t br = rng.byte(), bg = rng.byte(), bb = rng.byte(), ba = rng.byte();
        std::uint8_t ovr = rng.byte(), ovg = rng.byte(), ovb = rng.byte(), ova = rng.byte();
        bool replace = (rng.next() & 1) != 0;

        std::uint8_t kr = br, kg = bg, kb = bb, ka = ba;
        blendPixelFast(kr, kg, kb, ka, ovr, ovg, ovb, ova, lut, replace);

        std::uint8_t er = br, eg = bg, eb = bb, ea = ba;
        if (replace) replacePixel(er, eg, eb, ea, ovr, ovg, ovb, ova);
        else overlayPixel(er, eg, eb, ea, ovr, ovg, ovb, ova);

        if (kr != er || kg != eg || kb != eb || ka != ea) {
            std::cout << "FAIL randomized_blend_differential iter " << i
                      << (replace ? " replace" : " overlay") << "\n";
            return false;
        }
    }
    return true;
}

// Banda de overlay sobre imagen completa: paridad con OverlayTinter.
bool overlay_band_full_image() {
    constexpr int W = 16, H = 8;
    FrameImage dst(W, H), ov(W, H);
    dst.clear({10, 10, 10, 255});
    ov.clear({0, 0, 0, 0});
    Rng rng(1234);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if ((x + y) % 2 == 0)
                ov.setAt(x, y, {rng.byte(), rng.byte(), rng.byte(), rng.byte()});

    FrameImage ref = dst;  // copia para el camino de referencia
    AlphaLut lut = AlphaLut::make();
    PrecomputedTint spec = PrecomputedTint::make(200, 120, 60, 160.0f, 1.0f, 0.0f);

    applyOverlayBand(dst.data(), W, ov.data(), W, 0, H, W, spec, lut, false);

    // Referencia pixel a pixel con TintMath directo.
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            auto o = ov.at(x, y);
            if (o.a == 0) continue;
            std::uint8_t tr, tg, tb;
            tintByLuminance(o.r, o.g, o.b, cocos2d::ccColor3B(200, 120, 60),
                            160.0f, 1.0f, 0.0f, tr, tg, tb);
            auto d = ref.at(x, y);
            overlayPixel(d.r, d.g, d.b, d.a, tr, tg, tb, o.a);
            ref.setAt(x, y, {d.r, d.g, d.b, d.a});
        }
    }
    CHECK(dst == ref);
    return true;
}

}  // namespace

int main() {
    int passed = 0, total = 0;
#define RUN(fn) do { ++total; std::cout << (#fn) << "... "; \
    if (fn()) { ++passed; std::cout << "ok\n"; } else { ++g_failures; std::cout << "FALLO\n"; } } while (0)

    RUN(tint_scale_known_value);
    RUN(tint_red_clamps);
    RUN(outline_passthrough);
    RUN(transparent_and_dark_skipped);
    RUN(circle_coverage_and_partition);
    RUN(thin_diagonal_no_gaps);
    RUN(randomized_tint_differential);
    RUN(randomized_blend_differential);
    RUN(overlay_band_full_image);

    std::cout << passed << "/" << total << " tests de pintado OK\n";
    return (passed == total) ? 0 : 1;
}
