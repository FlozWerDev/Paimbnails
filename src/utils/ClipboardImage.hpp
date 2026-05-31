#pragma once

#include <cstdint>

namespace paimon {

// Copia un buffer RGBA (top-down, row major, sin padding) al portapapeles
// del sistema como imagen.
//
// El RGBA debe estar en orden R,G,B,A byte-a-byte (mismo formato que entrega
// FramebufferCapture tras `flipVertical`).
//
// Implementacion:
//   - Windows: registra TRES formatos en una sola apertura de clipboard:
//       1. CF_DIBV5 (BITMAPV5HEADER, 32bpp BGRA, BI_BITFIELDS con masks
//          explicitos y alpha valido) — preferido por apps modernas
//          (Photoshop, GIMP, Paint.NET, etc).
//       2. CF_DIB (BITMAPINFOHEADER, 24bpp BGR, bottom-up clasico) —
//          fallback universal para apps legacy / paint vanilla / Office.
//       3. "PNG" (registered format) — preferido por Discord, Slack,
//          navegadores web y cualquier app que llame
//          `RegisterClipboardFormat("PNG")`.
//   - Otras plataformas: stub que devuelve false.
//
// Devuelve true si AL MENOS UN formato se registro con exito. Llamar
// desde un thread cualquiera; usa el HWND del foreground como owner.
bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height);

} // namespace paimon
