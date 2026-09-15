// Tests de PackGen v2 con los assets OFICIALES del juego.
//
// Lee las hojas reales de Geometry Dash (PNG + .plist) y comprueba el
// nucleo sobre pixeles y marcos de verdad: tinte de iconos de jugador,
// overlay con un glow oficial y re-empaquetado de los 1698 marcos de
// GJ_GameSheetIcons. Tambien vuelca PPMs de antes/despues a la carpeta
// de salida para inspeccion visual.
//
// Compila sin Geode (nucleo puro + stub de ccTypes + stb_image):
//   g++ -std=c++17 -O2 -I tests/packgen_stubs -o /tmp/pgoff tests/packgen_official_assets.cpp
//   /tmp/pgoff [dir-Resources] [dir-salida]
//
// Cada prueba es una funcion bool. main() las ejecuta todas y devuelve 0 si
// pasan o 1 si alguna falla.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "../src/utils/stb_image.h"

#include "../src/features/texture-studio/packgen/ContentHash.hpp"
#include "../src/features/texture-studio/packgen/FrameImage.hpp"
#include "../src/features/texture-studio/packgen/MaxRectsPacker.hpp"
#include "../src/features/texture-studio/packgen/TintEngine.hpp"
#include "../src/features/texture-studio/engine/TintMath.hpp"

using namespace paimon::texture_studio::packgen;
using namespace paimon::texture_studio::tintmath;

namespace {

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #cond "\n"; \
        return false; \
    } \
} while (0)

// ---------------------------------------------------------------------------
// Plist minimo: solo marcos, rect, rotacion y tamano fuente.
// Soporta formato TexturePacker (textureRect/textureRotated/spriteSourceSize)
// y formato cocos2d clasico (frame/rotated/sourceSize).
// ---------------------------------------------------------------------------

struct PlistFrame {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;
    bool rotated = false;
    int trimW = 0, trimH = 0;  // spriteSize: sprite recortado (enderezado)
    int srcW = 0, srcH = 0;    // spriteSourceSize: original (metadata ruidosa)
};

std::string readFile(std::string const& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::string s;
    if (n > 0) {
        s.resize(static_cast<std::size_t>(n));
        s.resize(std::fread(s.data(), 1, s.size(), f));
    }
    std::fclose(f);
    return s;
}

// "{{a,b},{c,d}}" (con o sin espacios) -> a,b,c,d.
bool parseRect4(std::string const& xml, std::size_t from, int& a, int& b,
                int& c, int& d) {
    std::size_t l = xml.find("{{", from);
    if (l == std::string::npos) return false;
    std::size_t r = xml.find("}}", l);
    if (r == std::string::npos) return false;
    std::string inner = xml.substr(l, r - l + 2);
    std::string nospace;
    for (char ch : inner)
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') nospace += ch;
    return std::sscanf(nospace.c_str(), "{{%d,%d},{%d,%d}}", &a, &b, &c, &d) == 4;
}

bool parsePair(std::string const& xml, std::size_t from, int& a, int& b) {
    std::size_t l = xml.find('{', from);
    if (l == std::string::npos) return false;
    std::size_t r = xml.find('}', l);
    if (r == std::string::npos) return false;
    std::string inner = xml.substr(l, r - l + 1);
    std::string nospace;
    for (char ch : inner)
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') nospace += ch;
    return std::sscanf(nospace.c_str(), "{%d,%d}", &a, &b) == 2;
}

std::vector<PlistFrame> parsePlist(std::string const& xml) {
    std::vector<PlistFrame> out;
    std::size_t pos = 0;
    while (true) {
        std::size_t k = xml.find("<key>", pos);
        if (k == std::string::npos) break;
        std::size_t ke = xml.find("</key>", k);
        if (ke == std::string::npos) break;
        std::string key = xml.substr(k + 5, ke - k - 5);
        pos = ke + 6;
        if (key.size() < 5 || key.compare(key.size() - 4, 4, ".png") != 0)
            continue;
        // El dict del marco termina en el primer </dict> (sin dicts anidados).
        std::size_t de = xml.find("</dict>", pos);
        if (de == std::string::npos) break;
        std::string chunk = xml.substr(pos, de - pos);

        PlistFrame f;
        f.name = key;
        std::size_t rp = chunk.find("textureRect");
        if (rp == std::string::npos) rp = chunk.find("<key>frame</key>");
        if (rp == std::string::npos) continue;
        if (!parseRect4(chunk, rp, f.x, f.y, f.w, f.h)) continue;
        std::size_t op = chunk.find("textureRotated");
        if (op == std::string::npos) op = chunk.find("<key>rotated</key>");
        if (op != std::string::npos) {
            std::size_t t = chunk.find("<true/>", op);
            std::size_t fl = chunk.find("<false/>", op);
            f.rotated = (t != std::string::npos &&
                         (fl == std::string::npos || t < fl));
        }
        std::size_t sp = chunk.find("spriteSourceSize");
        if (sp == std::string::npos) sp = chunk.find("sourceSize");
        if (sp == std::string::npos) sp = chunk.find("spriteSize");
        if (sp != std::string::npos) parsePair(chunk, sp, f.srcW, f.srcH);
        std::size_t tp = chunk.find("<key>spriteSize</key>");
        if (tp == std::string::npos) tp = chunk.find("spriteSize");
        if (tp != std::string::npos) parsePair(chunk, tp, f.trimW, f.trimH);
        out.push_back(std::move(f));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Hoja oficial: PNG decodificado + marcos.
// ---------------------------------------------------------------------------

struct Sheet {
    std::string label;
    std::vector<std::uint8_t> px;
    int w = 0, h = 0;
    std::vector<PlistFrame> frames;

    bool load(std::string const& resDir, std::string const& base) {
        label = base;
        std::string pngPath = resDir + "/" + base + ".png";
        std::string plistPath = resDir + "/" + base + ".plist";
        int comp = 0;
        std::uint8_t* data = stbi_load(pngPath.c_str(), &w, &h, &comp, 4);
        if (!data) {
            std::cout << "no se pudo cargar " << pngPath << "\n";
            return false;
        }
        px.assign(data, data + static_cast<std::size_t>(w) * h * 4);
        stbi_image_free(data);
        frames = parsePlist(readFile(plistPath));
        return !frames.empty();
    }

    PlistFrame const* find(std::string const& name) const {
        for (auto const& f : frames)
            if (f.name == name) return &f;
        return nullptr;
    }
};

// Extrae el marco a FrameImage enderezado. Convencion verificada pixel a
// pixel sobre la hoja real (bird_01_001: caja 16x37 en (842,276) con el ovni
// completo y margenes transparentes): `textureRect` trae las dimensiones del
// sprite enderezado SIEMPRE; si `rotated` es true la caja guardada es
// (x,y,h,w) con los pixeles girados 90 CCW, y se endereza con un CW90.
FrameImage extractFrame(Sheet const& s, PlistFrame const& f) {
    if (f.w <= 0 || f.h <= 0) return {};
    int sw = f.rotated ? f.h : f.w;  // dims guardadas (caja real en la hoja)
    int sh = f.rotated ? f.w : f.h;
    FrameImage tmp(sw, sh);
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            int sx = f.x + x, sy = f.y + y;
            if (sx < 0 || sy < 0 || sx >= s.w || sy >= s.h) continue;
            auto const* p = s.px.data() +
                (static_cast<std::size_t>(sy) * s.w + sx) * 4;
            tmp.setAt(x, y, {p[0], p[1], p[2], p[3]});
        }
    }
    if (f.rotated) tmp.rotateCW90();  // guardado CCW -> enderezado
    return tmp;
}

// Dimensiones enderezadas = las listadas (el swap solo afecta a la caja
// guardada, nunca al sprite).
int frameW(PlistFrame const& f) { return f.w; }
int frameH(PlistFrame const& f) { return f.h; }

bool writePpm(std::string const& path, FrameImage const& img) {
    if (img.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n# oficial %dx%d\n%d %d\n255\n",
                 img.width(), img.height(), img.width(), img.height());
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            auto p = img.at(x, y);
            std::uint8_t r = p.r, g = p.g, b = p.b;
            if (p.a == 0) {
                bool dark = ((x / 8) + (y / 8)) % 2 == 0;
                r = g = b = dark ? 32 : 64;
            }
            std::fputc(r, f);
            std::fputc(g, f);
            std::fputc(b, f);
        }
    }
    std::fclose(f);
    return true;
}

Sheet g_icons, g_glow, g_game;
std::string g_outDir;

std::size_t opaqueCount(FrameImage const& img) {
    std::size_t n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.at(x, y).a != 0) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

bool official_plist_parse() {
    CHECK(!g_icons.frames.empty() && !g_glow.frames.empty() &&
          !g_game.frames.empty());
    std::cout << "  [marcos: icons=" << g_icons.frames.size()
              << " glow=" << g_glow.frames.size()
              << " game=" << g_game.frames.size() << "]\n";

    // Marcos conocidos que el test necesita mas abajo.
    CHECK(g_icons.find("player_01_001.png") != nullptr);
    CHECK(g_icons.find("player_01_2_001.png") != nullptr);
    CHECK(g_icons.find("ship_01_001.png") != nullptr);
    CHECK(g_glow.find("blackCogwheel_01_glow_001.png") != nullptr);

    // Cada marco cabe en su textura, con la caja GUARDADA (swap si rotado,
    // misma convencion que extractFrame). Ademas el marco enderezado debe
    // coincidir con spriteSize (recortado): se cumple en los 3542 marcos y
    // valida la convencion de rotacion en toda la hoja, no solo en el ovni.
    // El sourceSize trae metadata rota en 5 marcos (p. ej. spider_07_04 con
    // fuente {1,1} para un sprite de 3x3): se cuenta como aviso, no es fallo.
    int srcWarnings = 0;
    for (Sheet const* s : {&g_icons, &g_glow, &g_game}) {
        for (auto const& f : s->frames) {
            int sw = f.rotated ? f.h : f.w;
            int sh = f.rotated ? f.w : f.h;
            if (f.x < 0 || f.y < 0 || sw <= 0 || sh <= 0 ||
                f.x + sw > s->w || f.y + sh > s->h) {
                std::cout << "FAIL marco fuera de bounds: " << f.name << "\n";
                return false;
            }
            if (f.trimW > 0 && (f.w != f.trimW || f.h != f.trimH)) {
                std::cout << "FAIL marco != recortado: " << f.name << "\n";
                return false;
            }
            if (f.srcW > 0 && (f.w > f.srcW || f.h > f.srcH)) {
                if (srcWarnings < 8)
                    std::cout << "  [aviso fuente rota: " << f.name << " marco "
                              << f.w << "x" << f.h << " fuente " << f.srcW
                              << "x" << f.srcH << "]\n";
                ++srcWarnings;
            }
        }
    }
    std::cout << "  [avisos de fuente: " << srcWarnings << "]\n";
    return true;
}

bool official_rotation_geometry() {
    // Las naves miran a la derecha: mas anchas que altas. Si el marco viniera
    // rotado y lo enderezaramos mal, saldria alto y esto fallaria.
    auto const* ship = g_icons.find("ship_01_001.png");
    CHECK(ship != nullptr);
    FrameImage img = extractFrame(g_icons, *ship);
    std::cout << "  [ship_01_001: " << img.width() << "x" << img.height()
              << (ship->rotated ? " (rotado en hoja)" : " (sin rotar)") << "]\n";
    CHECK(img.width() >= img.height());
    CHECK(opaqueCount(img) > 0);

    // Marco rotado: bird_01_001 lista {{842,276},{37,16}} + rotated. El ovni
    // es mas ancho que alto enderezado; con la convencion contraria (caja
    // listada como guardada + CCW) saldria un 16x37 recortado de otro sprite.
    auto const* bird = g_icons.find("bird_01_001.png");
    CHECK(bird != nullptr && bird->rotated);
    FrameImage bimg = extractFrame(g_icons, *bird);
    CHECK(bimg.width() == 37 && bimg.height() == 16);
    CHECK(opaqueCount(bimg) > 100);
    // Simetria vertical del ovni (cupula centrada): mitad izq ~= mitad der.
    int left = 0, right = 0;
    for (int y = 0; y < bimg.height(); ++y)
        for (int x = 0; x < bimg.width(); ++x)
            if (bimg.at(x, y).a != 0)
                (x < bimg.width() / 2 ? left : right)++;
    CHECK(left > 0 && right > 0 &&
          std::abs(left - right) * 10 < (left + right));  // <10% asimetria

    std::size_t rotated = 0;
    for (auto const& f : g_icons.frames)
        if (f.rotated && f.w != f.h) ++rotated;
    std::cout << "  [marcos rotados no cuadrados en icons: " << rotated << "]\n";
    return true;
}

bool official_tint_player_icon() {
    struct Case {
        const char* name;
        std::uint8_t tr, tg, tb;
        float sat, con;
    };
    Case cases[] = {
        {"player_01_001.png", 50, 200, 50, 1.0f, 0.0f},    // verde primario
        {"player_01_2_001.png", 255, 255, 255, 1.0f, 0.0f},  // detalle blanco
        {"ship_01_001.png", 255, 128, 0, 1.2f, 0.1f},        // naranja con sat/con
    };
    AlphaLut lut = AlphaLut::make();
    for (auto const& c : cases) {
        auto const* f = g_icons.find(c.name);
        CHECK(f != nullptr);
        FrameImage src = extractFrame(g_icons, *f);
        CHECK(!src.empty() && opaqueCount(src) > 0);

        std::vector<std::uint8_t> mask(src.pixelCount(), 0);
        for (int y = 0; y < src.height(); ++y)
            for (int x = 0; x < src.width(); ++x)
                if (src.at(x, y).a != 0)
                    mask[static_cast<std::size_t>(y) * src.width() + x] = 255;

        FrameImage dst = src;
        PrecomputedTint spec =
            PrecomputedTint::make(c.tr, c.tg, c.tb, 160.0f, c.sat, c.con);
        std::size_t n = tintStackImage(
            src.data(), dst.data(), src.width(), src.height(), mask.data(),
            nullptr, nullptr, nullptr, spec, spec, spec, spec,
            false, false, 0, lut);
        // Cada pixel opaco se tintea exactamente una vez.
        CHECK(n == opaqueCount(src));

        // Referencia pixel a pixel con TintMath directo (tinte + overlay con
        // el valor de mascara como alfa). El kernel delega en las mismas
        // formulas: el alfa resultante es max(original, mascara), NO el
        // original preservado — LuminanceTinter usa este mismo kernel, asi
        // que esa es la conducta bit-exacta del motor, no un bug.
        cocos2d::ccColor3B tint(c.tr, c.tg, c.tb);
        for (int y = 0; y < src.height(); ++y) {
            for (int x = 0; x < src.width(); ++x) {
                auto s = src.at(x, y);
                std::uint8_t er = s.r, eg = s.g, eb = s.b, ea = s.a;
                if (s.a != 0) {
                    std::uint8_t tr, tg, tb;
                    tintByLuminance(s.r, s.g, s.b, tint, 160.0f, c.sat, c.con,
                                    tr, tg, tb);
                    overlayPixel(er, eg, eb, ea, tr, tg, tb, 255);
                }
                auto got = dst.at(x, y);
                if (got.r != er || got.g != eg || got.b != eb || got.a != ea) {
                    std::cout << "FAIL referencia en " << c.name
                              << " (" << x << "," << y << ")\n";
                    return false;
                }
            }
        }
        std::cout << "  [" << c.name << ": " << src.width() << "x"
                  << src.height() << ", " << n << " px]\n";

        // Vuelco visual con los colores del caso.
        std::string base = g_outDir + "/" + c.name;
        base.replace(base.end() - 4, base.end(), "");
        writePpm(base + "_original.ppm", src);
        writePpm(base + "_tintado.ppm", dst);
    }
    return true;
}

bool official_overlay_real_glow() {
    // El glow oficial mas grande como tinta de overlay sobre base oscura.
    PlistFrame const* best = nullptr;
    for (auto const& f : g_glow.frames) {
        if (f.w > 0 && f.h > 0 &&
            (!best || f.w * f.h > best->w * best->h))
            best = &f;
    }
    CHECK(best != nullptr);
    FrameImage glow = extractFrame(g_glow, *best);
    std::cout << "  [glow: " << best->name << " " << glow.width() << "x"
              << glow.height() << "]\n";

    constexpr int OX = 6, OY = 6;
    int W = glow.width() + OX * 2, H = glow.height() + OY * 2;
    FrameImage base(W, H), ov(W, H);
    base.clear({20, 20, 20, 255});
    ov.clear({0, 0, 0, 0});
    ov.blitOverwrite(OX, OY, glow);

    FrameImage dst = base;
    AlphaLut lut = AlphaLut::make();
    PrecomputedTint spec =
        PrecomputedTint::make(255, 220, 160, 160.0f, 1.0f, 0.0f);
    applyOverlayBand(dst.data(), W, ov.data(), W, 0, H, W, spec, lut, false);

    // Referencia pixel a pixel con TintMath directo.
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            auto o = ov.at(x, y);
            auto d = base.at(x, y);
            if (o.a != 0) {
                std::uint8_t tr, tg, tb;
                tintByLuminance(o.r, o.g, o.b,
                                cocos2d::ccColor3B(255, 220, 160),
                                160.0f, 1.0f, 0.0f, tr, tg, tb);
                overlayPixel(d.r, d.g, d.b, d.a, tr, tg, tb, o.a);
            }
            auto got = dst.at(x, y);
            if (got.r != d.r || got.g != d.g || got.b != d.b || got.a != d.a) {
                std::cout << "FAIL overlay en (" << x << "," << y << ")\n";
                return false;
            }
        }
    }
    writePpm(g_outDir + "/glow_overlay_resultado.ppm", dst);
    return true;
}

bool official_packer_icons() {
    std::vector<PackRect> rects;
    for (std::size_t i = 0; i < g_icons.frames.size(); ++i) {
        auto const& f = g_icons.frames[i];
        if (f.w <= 0 || f.h <= 0) continue;
        rects.push_back({frameW(f), frameH(f), static_cast<int>(i)});
    }
    CHECK(!rects.empty());

    MaxRectsPacker::Options opts;
    opts.padding = 2;
    opts.allowRotate = false;
    auto r = MaxRectsPacker(opts).pack(rects);
    CHECK(r.fits);
    CHECK(r.placements.size() == rects.size());

    // Sin solapes y contenidos.
    for (std::size_t i = 0; i < r.placements.size(); ++i) {
        auto const& a = r.placements[i];
        int aw = a.rotated ? a.h : a.w, ah = a.rotated ? a.w : a.h;
        if (a.x < 0 || a.y < 0 || a.x + aw > r.atlasW || a.y + ah > r.atlasH) {
            std::cout << "FAIL containment en marco " << i << "\n";
            return false;
        }
        for (std::size_t j = i + 1; j < r.placements.size(); ++j) {
            auto const& b = r.placements[j];
            int bw = b.rotated ? b.h : b.w, bh = b.rotated ? b.w : b.h;
            if (a.x < b.x + bw && b.x < a.x + aw && a.y < b.y + bh &&
                b.y < a.y + ah) {
                std::cout << "FAIL solape entre " << i << " y " << j << "\n";
                return false;
            }
        }
    }

    double used = 0;
    for (auto const& rc : rects) used += static_cast<double>(rc.w) * rc.h;
    double atlas = static_cast<double>(r.atlasW) * r.atlasH;
    double official =
        static_cast<double>(g_icons.w) * g_icons.h;
    std::cout << "  [packer icons: " << rects.size() << " marcos -> atlas "
              << r.atlasW << "x" << r.atlasH << ", uso "
              << (100.0 * used / atlas) << "%, hoja oficial "
              << g_icons.w << "x" << g_icons.h << " (ratio areas "
              << (atlas / official) << ")]\n";
    return true;
}

bool official_repack_roundtrip() {
    // Re-empaqueta los marcos reales y verifica que cada sprite sale
    // pixel a pixel identico al extraido de la hoja oficial.
    std::vector<PackRect> rects;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < g_icons.frames.size(); ++i) {
        auto const& f = g_icons.frames[i];
        if (f.w <= 0 || f.h <= 0) continue;
        rects.push_back({frameW(f), frameH(f), static_cast<int>(idx.size())});
        idx.push_back(i);
    }
    MaxRectsPacker::Options opts;
    opts.padding = 2;
    opts.allowRotate = true;
    auto r = MaxRectsPacker(opts).pack(rects);
    CHECK(r.fits);

    std::vector<FrameImage> src;
    src.reserve(idx.size());
    for (auto i : idx) src.push_back(extractFrame(g_icons, g_icons.frames[i]));

    FrameImage atlas(r.atlasW, r.atlasH);
    for (auto const& p : r.placements) {
        FrameImage tile = src[static_cast<std::size_t>(p.id)];
        int pw = p.rotated ? tile.height() : tile.width();
        int ph = p.rotated ? tile.width() : tile.height();
        if (p.rotated) tile.rotateCW90();
        CHECK(tile.width() == pw && tile.height() == ph);
        atlas.blitOverwrite(p.x, p.y, tile);
    }
    for (auto const& p : r.placements) {
        int pw = p.rotated ? p.h : p.w, ph = p.rotated ? p.w : p.h;
        FrameImage back = atlas.subRect(p.x, p.y, pw, ph);
        if (p.rotated) {
            back.rotateCW90();
            back.rotateCW90();
            back.rotateCW90();
        }
        if (back != src[static_cast<std::size_t>(p.id)]) {
            std::cout << "FAIL roundtrip en marco "
                      << g_icons.frames[idx[static_cast<std::size_t>(p.id)]].name
                      << "\n";
            return false;
        }
    }
    std::cout << "  [roundtrip: " << r.placements.size()
              << " marcos identicos, atlas " << r.atlasW << "x" << r.atlasH
              << "]\n";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const char* home = std::getenv("HOME");
    std::string resDir = (argc > 1)
        ? argv[1]
        : (std::string(home ? home : "") +
           "/.steam/steam/steamapps/common/Geometry Dash/Resources");
    g_outDir = (argc > 2) ? argv[2] : ".";

    std::cout << "assets: " << resDir << "\n";
    if (!g_icons.load(resDir, "GJ_GameSheetIcons")) return 1;
    if (!g_glow.load(resDir, "GJ_GameSheetGlow")) return 1;
    if (!g_game.load(resDir, "GJ_GameSheet")) return 1;
    std::cout << "hojas: icons " << g_icons.w << "x" << g_icons.h << ", glow "
              << g_glow.w << "x" << g_glow.h << ", game " << g_game.w << "x"
              << g_game.h << "\n";

    int passed = 0, total = 0;
#define RUN(fn) do { ++total; std::cout << (#fn) << "... "; \
    if (fn()) { ++passed; std::cout << "ok\n"; } else { std::cout << "FALLO\n"; } } while (0)

    RUN(official_plist_parse);
    RUN(official_rotation_geometry);
    RUN(official_tint_player_icon);
    RUN(official_overlay_real_glow);
    RUN(official_packer_icons);
    RUN(official_repack_roundtrip);

    std::cout << passed << "/" << total << " tests oficiales OK\n";
    return (passed == total) ? 0 : 1;
}
