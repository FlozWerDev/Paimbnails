#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────
// GifEncoder — codificador GIF89a animado minimo (LZW + paleta global).
//
// Por que existe: el pipeline de animacion del mod (AnimatedGIFSprite +
// GIFDecoder) solo entiende GIF. Para soportar cursores animados de Windows
// (.ani) hay que re-encodar sus frames RGBA a un GIF animado. stb_image_write
// no escribe GIF, asi que implementamos uno compacto aqui.
//
// Caracteristicas:
//  - Paleta global de hasta 255 colores (los mas frecuentes) + 1 indice
//    reservado para transparencia. Los pixeles con alpha < umbral se tratan
//    como transparentes (los cursores tienen bordes duros, 1-bit alpha basta).
//  - Loop infinito (extension NETSCAPE2.0).
//  - Delay por frame en centisegundos.
// ─────────────────────────────────────────────────────────────────────────

namespace paimon::gif {

struct EncodeFrame {
    int width = 0;
    int height = 0;
    int delayMs = 100;
    std::vector<uint8_t> rgba;   // width*height*4 RGBA8888 (top-down)
};

// Codifica los frames a un GIF89a animado. Todos los frames se redimensionan
// logicamente al tamano del primero (se asume que .ani usa frames del mismo
// tamano, lo cual es el caso normal). Devuelve los bytes del GIF o vacio si
// falla.
std::vector<uint8_t> encode(std::vector<EncodeFrame> const& frames,
                            uint8_t alphaThreshold = 128);

} // namespace paimon::gif
