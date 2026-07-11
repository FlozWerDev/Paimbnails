#include "ClipboardImage.hpp"
#include "ImageConverter.hpp"

#include <Geode/loader/Log.hpp>

#include <cstring>
#include <vector>

#ifdef GEODE_IS_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

namespace paimon {

#ifdef GEODE_IS_WINDOWS

namespace {

// Retry loop for opening the clipboard: other apps (clipboard managers,
// antivirus, RDP) may hold it briefly, making OpenClipboard fail with
// ACCESS_DENIED. Exponential backoff (~150ms worst case). Thread-safe;
// only called from workers (see CaptureOverlay.cpp).
bool openClipboardWithRetry(HWND owner) {
    static constexpr int kBackoffMs[] = {2, 4, 8, 16, 30, 30, 30, 30};
    static constexpr int kAttempts = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);
    for (int i = 0; i < kAttempts; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(kBackoffMs[i]);
    }
    return false;
}

// Allocate a movable global block, copy `data` into it, return the HGLOBAL.
// The caller transfers ownership to the clipboard via SetClipboardData; on
// failure it frees the block.
HGLOBAL allocAndFill(void const* data, size_t size) {
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return nullptr;
    void* dst = GlobalLock(hMem);
    if (!dst) {
        GlobalFree(hMem);
        return nullptr;
    }
    std::memcpy(dst, data, size);
    GlobalUnlock(hMem);
    return hMem;
}

// Build a CF_DIBV5 buffer: BITMAPV5HEADER + BGRA top-down pixel data.
// BI_BITFIELDS with explicit masks lets modern apps read the alpha channel.
std::vector<uint8_t> buildDIBV5(uint8_t const* rgba, int width, int height) {
    size_t const pixelBytes = static_cast<size_t>(width) * height * 4;
    std::vector<uint8_t> buf(sizeof(BITMAPV5HEADER) + pixelBytes);

    auto* h = reinterpret_cast<BITMAPV5HEADER*>(buf.data());
    h->bV5Size        = sizeof(BITMAPV5HEADER);
    h->bV5Width       = width;
    h->bV5Height      = -height; // negative = top-down
    h->bV5Planes      = 1;
    h->bV5BitCount    = 32;
    h->bV5Compression = BI_BITFIELDS;
    h->bV5SizeImage   = static_cast<DWORD>(pixelBytes);
    h->bV5RedMask     = 0x00FF0000;
    h->bV5GreenMask   = 0x0000FF00;
    h->bV5BlueMask    = 0x000000FF;
    h->bV5AlphaMask   = 0xFF000000;
    h->bV5CSType      = LCS_sRGB;
    h->bV5Intent      = LCS_GM_GRAPHICS;

    // convert RGBA -> BGRA into the destination
    uint8_t* dst = buf.data() + sizeof(BITMAPV5HEADER);
    for (int y = 0; y < height; ++y) {
        uint8_t const* srcRow = rgba + static_cast<size_t>(y) * width * 4;
        uint8_t* dstRow       = dst + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            uint8_t const* s = srcRow + x * 4;
            uint8_t* d       = dstRow + x * 4;
            d[0] = s[2]; // B
            d[1] = s[1]; // G
            d[2] = s[0]; // R
            d[3] = s[3]; // A
        }
    }
    return buf;
}

// Build a classic CF_DIB buffer: BITMAPINFOHEADER + 24bpp BGR bottom-up.
// Understood by Paint, Office, and any reader assuming legacy DIB.
std::vector<uint8_t> buildDIBClassic(uint8_t const* rgba, int width, int height) {
    int const rowStride = ((width * 3 + 3) & ~3); // pad to 4 bytes
    size_t const pixelBytes = static_cast<size_t>(rowStride) * height;
    std::vector<uint8_t> buf(sizeof(BITMAPINFOHEADER) + pixelBytes, 0);

    auto* h = reinterpret_cast<BITMAPINFOHEADER*>(buf.data());
    h->biSize        = sizeof(BITMAPINFOHEADER);
    h->biWidth       = width;
    h->biHeight      = height; // positive = bottom-up (universal compat)
    h->biPlanes      = 1;
    h->biBitCount    = 24;
    h->biCompression = BI_RGB;
    h->biSizeImage   = static_cast<DWORD>(pixelBytes);

    // bottom-up: DIB row 0 = last source row
    uint8_t* dst = buf.data() + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < height; ++y) {
        int const srcY = height - 1 - y;
        uint8_t const* srcRow = rgba + static_cast<size_t>(srcY) * width * 4;
        uint8_t* dstRow       = dst + static_cast<size_t>(y) * rowStride;
        for (int x = 0; x < width; ++x) {
            uint8_t const* s = srcRow + x * 4;
            uint8_t* d       = dstRow + x * 3;
            d[0] = s[2]; // B
            d[1] = s[1]; // G
            d[2] = s[0]; // R
        }
    }
    return buf;
}

} // namespace

bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
        geode::log::warn("[ClipboardImage] parametros invalidos: rgba={} {}x{}",
                         (void*)rgba, width, height);
        return false;
    }

    // 1. build all buffers before locking the clipboard to minimize lock time

    auto dibv5Buf  = buildDIBV5(rgba, width, height);
    auto dibClassicBuf = buildDIBClassic(rgba, width, height);

    std::vector<uint8_t> pngBuf;
    bool const pngOk = ImageConverter::rgbaToPngBuffer(
        rgba,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        pngBuf
    );

    // 2. allocate a global block per format; the clipboard takes ownership of each

    HGLOBAL hDIBV5    = allocAndFill(dibv5Buf.data(),     dibv5Buf.size());
    HGLOBAL hDIB      = allocAndFill(dibClassicBuf.data(), dibClassicBuf.size());
    HGLOBAL hPNG      = (pngOk && !pngBuf.empty())
        ? allocAndFill(pngBuf.data(), pngBuf.size())
        : nullptr;

    if (!hDIBV5 && !hDIB && !hPNG) {
        if (hDIBV5) GlobalFree(hDIBV5);
        if (hDIB)   GlobalFree(hDIB);
        if (hPNG)   GlobalFree(hPNG);
        geode::log::warn("[ClipboardImage] no se pudo alocar memoria para ningun formato");
        return false;
    }

    // 3. register the custom "PNG" format (Discord/browsers read it)

    UINT const cfPng = RegisterClipboardFormatA("PNG");

    // 4. open clipboard, empty, set the formats
    HWND owner = GetForegroundWindow();
    if (!openClipboardWithRetry(owner)) {
        geode::log::warn("[ClipboardImage] OpenClipboard fallo (GetLastError={})",
                         GetLastError());
        if (hDIBV5) GlobalFree(hDIBV5);
        if (hDIB)   GlobalFree(hDIB);
        if (hPNG)   GlobalFree(hPNG);
        return false;
    }

    if (!EmptyClipboard()) {
        geode::log::warn("[ClipboardImage] EmptyClipboard fallo (GetLastError={})",
                         GetLastError());
        CloseClipboard();
        if (hDIBV5) GlobalFree(hDIBV5);
        if (hDIB)   GlobalFree(hDIB);
        if (hPNG)   GlobalFree(hPNG);
        return false;
    }

    bool anySet = false;

    if (hDIBV5) {
        if (SetClipboardData(CF_DIBV5, hDIBV5)) {
            hDIBV5 = nullptr; // ownership transferred
            anySet = true;
        } else {
            geode::log::warn("[ClipboardImage] SetClipboardData(CF_DIBV5) fallo: {}",
                             GetLastError());
        }
    }
    if (hDIB) {
        if (SetClipboardData(CF_DIB, hDIB)) {
            hDIB = nullptr;
            anySet = true;
        } else {
            geode::log::warn("[ClipboardImage] SetClipboardData(CF_DIB) fallo: {}",
                             GetLastError());
        }
    }
    if (hPNG && cfPng != 0) {
        if (SetClipboardData(cfPng, hPNG)) {
            hPNG = nullptr;
            anySet = true;
        } else {
            geode::log::warn("[ClipboardImage] SetClipboardData(\"PNG\") fallo: {}",
                             GetLastError());
        }
    }

    CloseClipboard();

    // 5. free whatever wasn't transferred to the clipboard
    if (hDIBV5) GlobalFree(hDIBV5);
    if (hDIB)   GlobalFree(hDIB);
    if (hPNG)   GlobalFree(hPNG);

    if (anySet) {
        geode::log::info("[ClipboardImage] {}x{} copiado al portapapeles "
                         "(DIBV5={} DIB={} PNG={})",
                         width, height,
                         (hDIBV5 == nullptr) ? "ok" : "skip",
                         (hDIB == nullptr)   ? "ok" : "skip",
                         (hPNG == nullptr && pngOk) ? "ok" : "skip");
    }

    return anySet;
}

#else // !GEODE_IS_WINDOWS

bool copyRGBAToClipboard(uint8_t const* /*rgba*/, int /*width*/, int /*height*/) {
    // TODO: macOS (NSPasteboard / NSImage), Android (ClipboardManager).
    return false;
}

#endif

} // namespace paimon
