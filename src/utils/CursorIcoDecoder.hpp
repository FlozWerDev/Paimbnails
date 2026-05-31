#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────────────
// CursorIcoDecoder — decodifica cursores de Windows (.cur / .ico) y cursores
// animados (.ani) a RGBA8888.
//
// Por que existe: los packs de cursores de paginas como cursors-4u.com vienen
// en .zip con archivos .cur (estaticos) y .ani (animados). Ni stb_image ni
// CCImage de cocos2d entienden esos formatos, asi que el pipeline de imagenes
// del mod no los puede cargar directamente. Este decodificador los convierte a
// pixeles RGBA crudos que luego CursorManager re-encoda a PNG/GIF en la galeria.
//
// Formato:
//  - .ico/.cur: cabecera ICONDIR + ICONDIRENTRY[] + imagenes (BMP DIB o PNG).
//    .cur usa el campo "hotspot" en lugar de los planos/bpp del .ico, pero la
//    estructura del payload de imagen es identica.
//  - .ani: contenedor RIFF ("ACON") con una lista "fram" de chunks "icon",
//    cada uno es un .ico/.cur completo (un frame de la animacion).
// ─────────────────────────────────────────────────────────────────────────

namespace paimon::cursor_ico {

struct DecodedFrame {
    int width = 0;
    int height = 0;
    int delayMs = 100;            // duracion del frame (solo relevante en .ani)
    std::vector<uint8_t> rgba;    // width*height*4 RGBA8888 (top-down)
};

struct DecodeResult {
    bool success = false;
    bool animated = false;
    std::vector<DecodedFrame> frames;
    std::string error;
};

// Magic-byte detection ----------------------------------------------------

// .ico = "00 00 01 00", .cur = "00 00 02 00"
bool isIco(uint8_t const* data, size_t size);
bool isCur(uint8_t const* data, size_t size);
// .ani = "RIFF" .... "ACON"
bool isAni(uint8_t const* data, size_t size);

// True si la extension/contenido corresponde a un formato decodificable aqui.
bool isSupported(uint8_t const* data, size_t size);

// Decodifica un .ico/.cur. Elige la imagen de mayor resolucion disponible.
DecodeResult decodeIco(uint8_t const* data, size_t size);

// Decodifica un .ani completo (todos los frames, en orden de reproduccion).
DecodeResult decodeAni(uint8_t const* data, size_t size);

// Punto de entrada generico: detecta el formato y delega.
DecodeResult decode(uint8_t const* data, size_t size);

} // namespace paimon::cursor_ico
