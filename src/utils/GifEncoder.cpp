#include "GifEncoder.hpp"
#include <unordered_map>
#include <algorithm>
#include <climits>
#include <cstring>

namespace paimon::gif {

namespace {

// Empaquetador de bits LSB-first para los codigos LZW del GIF.
struct BitWriter {
    std::vector<uint8_t>& out;
    uint32_t bitBuffer = 0;
    int bitCount = 0;
    std::vector<uint8_t> block; // sub-bloque actual (max 255 bytes)

    explicit BitWriter(std::vector<uint8_t>& o) : out(o) {}

    void writeBits(int code, int len) {
        bitBuffer |= (static_cast<uint32_t>(code) << bitCount);
        bitCount += len;
        while (bitCount >= 8) {
            block.push_back(static_cast<uint8_t>(bitBuffer & 0xFF));
            bitBuffer >>= 8;
            bitCount -= 8;
            if (block.size() == 255) flushBlock();
        }
    }

    void flushBlock() {
        if (block.empty()) return;
        out.push_back(static_cast<uint8_t>(block.size()));
        out.insert(out.end(), block.begin(), block.end());
        block.clear();
    }

    void finish() {
        if (bitCount > 0) {
            block.push_back(static_cast<uint8_t>(bitBuffer & 0xFF));
            bitBuffer = 0;
            bitCount = 0;
        }
        flushBlock();
        out.push_back(0x00); // block terminator
    }
};

// LZW de un frame de indices (paleta) -> stream comprimido GIF.
void lzwEncode(std::vector<uint8_t> const& indices, int minCodeSize,
               std::vector<uint8_t>& out) {
    int clearCode = 1 << minCodeSize;
    int endCode = clearCode + 1;
    int codeSize = minCodeSize + 1;
    int nextCode = endCode + 1;

    out.push_back(static_cast<uint8_t>(minCodeSize));
    BitWriter writer(out);

    // Diccionario: clave = (prefijo << 8) | byte. Reiniciado tras Clear.
    std::unordered_map<uint32_t, int> dict;
    dict.reserve(4096);

    auto resetDict = [&]() {
        dict.clear();
        codeSize = minCodeSize + 1;
        nextCode = endCode + 1;
    };

    writer.writeBits(clearCode, codeSize);

    if (indices.empty()) {
        writer.writeBits(endCode, codeSize);
        writer.finish();
        return;
    }

    int prefix = indices[0];
    for (size_t i = 1; i < indices.size(); ++i) {
        uint8_t k = indices[i];
        uint32_t key = (static_cast<uint32_t>(prefix) << 8) | k;
        auto it = dict.find(key);
        if (it != dict.end()) {
            prefix = it->second;
        } else {
            writer.writeBits(prefix, codeSize);
            if (nextCode < 4096) {
                dict[key] = nextCode++;
                if (nextCode > (1 << codeSize) && codeSize < 12) {
                    codeSize++;
                }
            } else {
                writer.writeBits(clearCode, codeSize);
                resetDict();
            }
            prefix = k;
        }
    }
    writer.writeBits(prefix, codeSize);
    writer.writeBits(endCode, codeSize);
    writer.finish();
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

struct RGB { uint8_t r, g, b; };

} // namespace

std::vector<uint8_t> encode(std::vector<EncodeFrame> const& frames, uint8_t alphaThreshold) {
    std::vector<uint8_t> out;
    if (frames.empty()) return out;

    int W = frames[0].width;
    int H = frames[0].height;
    if (W <= 0 || H <= 0) return out;

    // ── 1. Cuantizacion: recopilar colores (alpha >= umbral) por frecuencia ──
    // Indice 0 reservado para transparencia. Hasta 255 colores opacos.
    std::unordered_map<uint32_t, uint32_t> freq;
    freq.reserve(1024);
    for (auto const& f : frames) {
        if (f.width != W || f.height != H) continue; // se ignoran tamanos raros
        size_t n = static_cast<size_t>(W) * H;
        for (size_t i = 0; i < n; ++i) {
            uint8_t a = f.rgba[i * 4 + 3];
            if (a < alphaThreshold) continue;
            uint32_t key = (static_cast<uint32_t>(f.rgba[i*4+0]) << 16)
                         | (static_cast<uint32_t>(f.rgba[i*4+1]) << 8)
                         |  static_cast<uint32_t>(f.rgba[i*4+2]);
            freq[key]++;
        }
    }

    std::vector<std::pair<uint32_t,uint32_t>> sorted(freq.begin(), freq.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto const& a, auto const& b){ return a.second > b.second; });

    // Paleta: indice 0 = transparente (negro placeholder), 1..N colores.
    std::vector<RGB> palette;
    palette.push_back({0, 0, 0}); // transparente
    for (auto const& [key, _] : sorted) {
        if (palette.size() >= 256) break;
        palette.push_back({
            static_cast<uint8_t>((key >> 16) & 0xFF),
            static_cast<uint8_t>((key >> 8) & 0xFF),
            static_cast<uint8_t>(key & 0xFF)
        });
    }

    // Tamano de paleta GIF debe ser potencia de 2 (2..256).
    int palBits = 1;
    while ((1 << palBits) < static_cast<int>(palette.size())) palBits++;
    if (palBits < 1) palBits = 1;
    int palSize = 1 << palBits;

    // Mapa exacto color->indice para acelerar (los frames suelen repetir colores).
    std::unordered_map<uint32_t, uint8_t> colorToIdx;
    for (size_t i = 1; i < palette.size(); ++i) {
        uint32_t key = (static_cast<uint32_t>(palette[i].r) << 16)
                     | (static_cast<uint32_t>(palette[i].g) << 8)
                     |  static_cast<uint32_t>(palette[i].b);
        colorToIdx[key] = static_cast<uint8_t>(i);
    }

    auto nearestIndex = [&](uint8_t r, uint8_t g, uint8_t b) -> uint8_t {
        uint32_t key = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
        auto it = colorToIdx.find(key);
        if (it != colorToIdx.end()) return it->second;
        // Busqueda lineal por color mas cercano (paleta pequena, frames pequenos).
        int best = 1; long bestDist = LONG_MAX;
        for (size_t i = 1; i < palette.size(); ++i) {
            long dr = static_cast<long>(r) - palette[i].r;
            long dg = static_cast<long>(g) - palette[i].g;
            long db = static_cast<long>(b) - palette[i].b;
            long d = dr*dr + dg*dg + db*db;
            if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
        }
        colorToIdx[key] = static_cast<uint8_t>(best);
        return static_cast<uint8_t>(best);
    };

    // ── 2. Cabecera GIF89a ──
    out.insert(out.end(), {'G','I','F','8','9','a'});
    put16(out, static_cast<uint16_t>(W));
    put16(out, static_cast<uint16_t>(H));
    // Packed: global color table flag (1) | color res (palBits-1)<<4 | sort 0 | size (palBits-1)
    uint8_t packed = 0x80 | ((palBits - 1) << 4) | (palBits - 1);
    out.push_back(packed);
    out.push_back(0);  // background color index
    out.push_back(0);  // pixel aspect ratio

    // Global color table (palSize * 3 bytes)
    for (int i = 0; i < palSize; ++i) {
        if (i < static_cast<int>(palette.size())) {
            out.push_back(palette[i].r);
            out.push_back(palette[i].g);
            out.push_back(palette[i].b);
        } else {
            out.push_back(0); out.push_back(0); out.push_back(0);
        }
    }

    // ── 3. Loop infinito (NETSCAPE2.0) ──
    out.insert(out.end(), {0x21, 0xFF, 0x0B});
    char const* nsle = "NETSCAPE2.0";
    out.insert(out.end(), nsle, nsle + 11);
    out.insert(out.end(), {0x03, 0x01, 0x00, 0x00, 0x00});

    int minCodeSize = std::max(2, palBits);

    // ── 4. Cada frame ──
    for (auto const& f : frames) {
        if (f.width != W || f.height != H) continue;

        // Graphic Control Extension (delay + transparencia)
        int delayCs = std::max(2, f.delayMs / 10); // centisegundos, min 2
        out.insert(out.end(), {0x21, 0xF9, 0x04});
        // packed: disposal=2 (restore to bg) <<2 | transparent color flag (1)
        out.push_back(static_cast<uint8_t>((2 << 2) | 0x01));
        put16(out, static_cast<uint16_t>(delayCs));
        out.push_back(0x00); // transparent color index = 0
        out.push_back(0x00); // block terminator

        // Image Descriptor
        out.push_back(0x2C);
        put16(out, 0); // left
        put16(out, 0); // top
        put16(out, static_cast<uint16_t>(W));
        put16(out, static_cast<uint16_t>(H));
        out.push_back(0x00); // no local color table

        // Indices del frame
        std::vector<uint8_t> indices(static_cast<size_t>(W) * H, 0);
        size_t n = static_cast<size_t>(W) * H;
        for (size_t i = 0; i < n; ++i) {
            uint8_t a = f.rgba[i * 4 + 3];
            if (a < alphaThreshold) {
                indices[i] = 0; // transparente
            } else {
                indices[i] = nearestIndex(f.rgba[i*4+0], f.rgba[i*4+1], f.rgba[i*4+2]);
            }
        }

        lzwEncode(indices, minCodeSize, out);
    }

    // Trailer
    out.push_back(0x3B);
    return out;
}

} // namespace paimon::gif
