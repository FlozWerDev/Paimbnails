#include "ClipboardImage.hpp"
#include "ImageConverter.hpp"

#include <Geode/loader/Log.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef GEODE_IS_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>
#endif

namespace paimon {

#ifdef GEODE_IS_WINDOWS

namespace {

// retry openclipboard briefly; clipboard managers, av or rdp may hold it.
bool openClipboardWithRetry(HWND owner) {
    static constexpr int kBackoffMs[] = {2, 4, 8, 16, 30, 30, 30, 30};
    static constexpr int kAttempts = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);
    for (int i = 0; i < kAttempts; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(kBackoffMs[i]);
    }
    return false;
}

// movable block for setclipboarddata; caller frees on failure.
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

    uint8_t* dst = buf.data() + sizeof(BITMAPV5HEADER);
    for (int y = 0; y < height; ++y) {
        uint8_t const* srcRow = rgba + static_cast<size_t>(y) * width * 4;
        uint8_t* dstRow       = dst + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            uint8_t const* s = srcRow + x * 4;
            uint8_t* d       = dstRow + x * 4;
    d[0] = s[2];
    d[1] = s[1];
    d[2] = s[0];
    d[3] = s[3];
        }
    }
    return buf;
}

// legacy cf_dib: 24bpp bottom-up bgr for broad compatibility.
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

    // bottom-up: dib row 0 is the last source row.
    uint8_t* dst = buf.data() + sizeof(BITMAPINFOHEADER);
    for (int y = 0; y < height; ++y) {
        int const srcY = height - 1 - y;
        uint8_t const* srcRow = rgba + static_cast<size_t>(srcY) * width * 4;
        uint8_t* dstRow       = dst + static_cast<size_t>(y) * rowStride;
        for (int x = 0; x < width; ++x) {
            uint8_t const* s = srcRow + x * 4;
            uint8_t* d       = dstRow + x * 3;
    d[0] = s[2];
    d[1] = s[1];
    d[2] = s[0];
        }
    }
    return buf;
}

}

bool copyRGBAToClipboard(uint8_t const* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
        geode::log::warn("[ClipboardImage] invalid params: rgba={} {}x{}",
                         (void*)rgba, width, height);
        return false;
    }

    auto dibv5Buf  = buildDIBV5(rgba, width, height);
    auto dibClassicBuf = buildDIBClassic(rgba, width, height);

    std::vector<uint8_t> pngBuf;
    bool const pngOk = ImageConverter::rgbaToPngBuffer(
        rgba,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        pngBuf
    );


    HGLOBAL hDIBV5    = allocAndFill(dibv5Buf.data(),     dibv5Buf.size());
    HGLOBAL hDIB      = allocAndFill(dibClassicBuf.data(), dibClassicBuf.size());
    HGLOBAL hPNG      = (pngOk && !pngBuf.empty())
        ? allocAndFill(pngBuf.data(), pngBuf.size())
        : nullptr;

    if (!hDIBV5 && !hDIB && !hPNG) {
        if (hDIBV5) GlobalFree(hDIBV5);
        if (hDIB)   GlobalFree(hDIB);
        if (hPNG)   GlobalFree(hPNG);
        geode::log::warn("[ClipboardImage] could not allocate memory for any format");
        return false;
    }

    // custom png format for discord and browsers.
    UINT const cfPng = RegisterClipboardFormatA("PNG");

    HWND owner = GetForegroundWindow();
    if (!openClipboardWithRetry(owner)) {
        geode::log::warn("[ClipboardImage] OpenClipboard failed (GetLastError={})",
                         GetLastError());
        if (hDIBV5) GlobalFree(hDIBV5);
        if (hDIB)   GlobalFree(hDIB);
        if (hPNG)   GlobalFree(hPNG);
        return false;
    }

    if (!EmptyClipboard()) {
        geode::log::warn("[ClipboardImage] EmptyClipboard failed (GetLastError={})",
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
            geode::log::warn("[ClipboardImage] SetClipboardData(CF_DIBV5) failed: {}",
                             GetLastError());
        }
    }
    if (hDIB) {
        if (SetClipboardData(CF_DIB, hDIB)) {
            hDIB = nullptr;
            anySet = true;
        } else {
            geode::log::warn("[ClipboardImage] SetClipboardData(CF_DIB) failed: {}",
                             GetLastError());
        }
    }
    if (hPNG && cfPng != 0) {
        if (SetClipboardData(cfPng, hPNG)) {
            hPNG = nullptr;
            anySet = true;
        } else {
            geode::log::warn("[ClipboardImage] SetClipboardData(\"PNG\") failed: {}",
                             GetLastError());
        }
    }

    CloseClipboard();

    if (hDIBV5) GlobalFree(hDIBV5);
    if (hDIB)   GlobalFree(hDIB);
    if (hPNG)   GlobalFree(hPNG);

    if (anySet) {
        geode::log::info("[ClipboardImage] {}x{} copied to clipboard "
                         "(DIBV5={} DIB={} PNG={})",
                         width, height,
                         (hDIBV5 == nullptr) ? "ok" : "skip",
                         (hDIB == nullptr)   ? "ok" : "skip",
                         (hPNG == nullptr && pngOk) ? "ok" : "skip");
    }

    return anySet;
}

namespace {

std::vector<UINT> encodedImageFormats() {
    static UINT const fPng  = RegisterClipboardFormatA("PNG");
    static UINT const fMPng = RegisterClipboardFormatA("image/png");
    static UINT const fGif  = RegisterClipboardFormatA("GIF");
    static UINT const fMGif = RegisterClipboardFormatA("image/gif");
    std::vector<UINT> out;
    for (UINT f : {fGif, fMGif, fPng, fMPng}) {
        if (f != 0) out.push_back(f);
    }
    return out;
}

bool readClipboardEncoded(UINT format, ClipboardImageData& out) {
    HANDLE h = GetClipboardData(format);
    if (!h) return false;
    SIZE_T size = GlobalSize(h);
    constexpr SIZE_T kMaxClipboardBytes = 16u * 1024u * 1024u;
    if (size < 12 || size > kMaxClipboardBytes) return false;
    void const* ptr = GlobalLock(h);
    if (!ptr) return false;
    auto const* bytes = static_cast<uint8_t const*>(ptr);
    out.encodedBytes.assign(bytes, bytes + size);
    GlobalUnlock(h);
    out.isEncodedFile = true;
    return true;
}

bool isSupportedImageExt(std::filesystem::path const& p) {
    auto ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
           ext == L".webp" || ext == L".gif";
}

std::filesystem::path firstClipboardDropPath() {
    HANDLE h = GetClipboardData(CF_HDROP);
    if (!h) return {};
    auto drop = static_cast<HDROP>(GlobalLock(h));
    if (!drop) return {};
    std::filesystem::path result;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
    if (count > 0) {
        UINT len = DragQueryFileW(drop, 0, nullptr, 0);
        if (len > 0 && len < 32768) {
            std::wstring pathW(len + 1, L'\0');
            if (DragQueryFileW(drop, 0, pathW.data(), len + 1) > 0) {
                pathW.resize(len);
                result = std::filesystem::path(pathW);
            }
        }
    }
    GlobalUnlock(h);
    return result;
}

bool readFileDropImage(std::filesystem::path const& path, ClipboardImageData& out) {
    if (path.empty() || !isSupportedImageExt(path)) return false;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    auto endPos = file.tellg();
    constexpr std::streamoff kMaxFileBytes = 16 * 1024 * 1024;
    if (endPos < 12 || endPos > kMaxFileBytes) return false;
    auto size = static_cast<size_t>(endPos);
    out.encodedBytes.resize(size);
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(out.encodedBytes.data()), static_cast<std::streamsize>(size))) {
        out.encodedBytes.clear();
        return false;
    }
    out.isEncodedFile = true;
    return true;
}

uint8_t extractMaskedChannel(uint32_t pixel, uint32_t mask) {
    if (mask == 0) return 0;
    unsigned int shift = 0;
    uint32_t m = mask;
    while ((m & 1u) == 0u) {
        m >>= 1u;
        ++shift;
    }
    unsigned int bits = 0;
    while ((m & 1u) != 0u) {
        m >>= 1u;
        ++bits;
    }
    if (bits == 0) return 0;
    uint32_t val = (pixel & mask) >> shift;
    uint32_t maxVal = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    return static_cast<uint8_t>((static_cast<uint64_t>(val) * 255u + maxVal / 2u) / maxVal);
}

bool decodeDIBToRGBA(uint8_t const* data, size_t size, ClipboardImageData& out) {
    if (!data || size < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFOHEADER bih{};
    std::memcpy(&bih, data, sizeof(BITMAPINFOHEADER));
    if (bih.biSize < sizeof(BITMAPINFOHEADER) || bih.biSize > size) return false;
    if (bih.biPlanes != 1 || (bih.biBitCount != 24 && bih.biBitCount != 32)) return false;

    int const width = bih.biWidth;
    bool const topDown = bih.biHeight < 0;
    int64_t const absH = topDown
        ? -static_cast<int64_t>(bih.biHeight)
        : static_cast<int64_t>(bih.biHeight);
    if (width <= 0 || width > 8192 || absH <= 0 || absH > 8192) return false;
    int const height = static_cast<int>(absH);

    size_t offset = bih.biSize;
    uint32_t redMask = 0x00FF0000u;
    uint32_t greenMask = 0x0000FF00u;
    uint32_t blueMask = 0x000000FFu;
    uint32_t alphaMask = 0u;
    bool useBitfields = false;

    constexpr DWORD kBiAlphaBitfields = 6;
    if (bih.biCompression == BI_BITFIELDS || bih.biCompression == kBiAlphaBitfields) {
        useBitfields = true;
        if (bih.biSize >= sizeof(BITMAPV4HEADER)) {
            BITMAPV4HEADER v4{};
            std::memcpy(&v4, data, sizeof(BITMAPV4HEADER));
            redMask = v4.bV4RedMask;
            greenMask = v4.bV4GreenMask;
            blueMask = v4.bV4BlueMask;
            alphaMask = v4.bV4AlphaMask;
        } else if (bih.biSize == sizeof(BITMAPINFOHEADER)) {
            size_t const maskBytes = (bih.biCompression == kBiAlphaBitfields) ? 16 : 12;
            if (offset > size || maskBytes > size - offset) return false;
            std::memcpy(&redMask, data + offset + 0, 4);
            std::memcpy(&greenMask, data + offset + 4, 4);
            std::memcpy(&blueMask, data + offset + 8, 4);
            if (maskBytes == 16) std::memcpy(&alphaMask, data + offset + 12, 4);
            offset += maskBytes;
        } else {
            return false;
        }
        if (redMask == 0 || greenMask == 0 || blueMask == 0) return false;
    } else if (bih.biCompression != BI_RGB) {
        return false;
    }

    if (bih.biClrUsed > 256) return false;
    size_t const paletteBytes = static_cast<size_t>(bih.biClrUsed) * 4u;
    if (offset > size || paletteBytes > size - offset) return false;
    offset += paletteBytes;

    size_t const srcStride = (bih.biBitCount == 32)
        ? static_cast<size_t>(width) * 4u
        : ((static_cast<size_t>(width) * 3u + 3u) & ~size_t(3u));
    if (offset > size || srcStride * static_cast<size_t>(height) > size - offset) {
        return false;
    }

    out.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    out.width = width;
    out.height = height;
    out.isEncodedFile = false;

    bool anyNonZeroAlpha = false;
    for (int y = 0; y < height; ++y) {
        int const srcY = topDown ? y : (height - 1 - y);
        uint8_t const* srcRow = data + offset + static_cast<size_t>(srcY) * srcStride;
        uint8_t* dstRow = out.rgba.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;

        if (bih.biBitCount == 32) {
            for (int x = 0; x < width; ++x) {
                uint32_t px = 0;
                std::memcpy(&px, srcRow + static_cast<size_t>(x) * 4u, 4);
                uint8_t* d = dstRow + static_cast<size_t>(x) * 4u;
                if (useBitfields) {
                    d[0] = extractMaskedChannel(px, redMask);
                    d[1] = extractMaskedChannel(px, greenMask);
                    d[2] = extractMaskedChannel(px, blueMask);
                    d[3] = alphaMask ? extractMaskedChannel(px, alphaMask) : 255;
                } else {
                    d[0] = static_cast<uint8_t>((px >> 16) & 0xFFu);
                    d[1] = static_cast<uint8_t>((px >> 8) & 0xFFu);
                    d[2] = static_cast<uint8_t>(px & 0xFFu);
                    d[3] = static_cast<uint8_t>((px >> 24) & 0xFFu);
                }
                if (d[3] != 0) anyNonZeroAlpha = true;
            }
        } else {
            for (int x = 0; x < width; ++x) {
                uint8_t const* s = srcRow + static_cast<size_t>(x) * 3u;
                uint8_t* d = dstRow + static_cast<size_t>(x) * 4u;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = 255;
            }
            anyNonZeroAlpha = true;
        }
    }

    if (bih.biBitCount == 32 && !anyNonZeroAlpha) {
        for (size_t i = 3; i < out.rgba.size(); i += 4) {
            out.rgba[i] = 255;
        }
    }
    return true;
}

bool readClipboardDIB(UINT format, ClipboardImageData& out) {
    HANDLE h = GetClipboardData(format);
    if (!h) return false;
    SIZE_T size = GlobalSize(h);
    constexpr SIZE_T kMaxDibBytes = 64u * 1024u * 1024u;
    if (size < sizeof(BITMAPINFOHEADER) || size > kMaxDibBytes) return false;
    void const* ptr = GlobalLock(h);
    if (!ptr) return false;
    bool ok = decodeDIBToRGBA(static_cast<uint8_t const*>(ptr), size, out);
    GlobalUnlock(h);
    return ok;
}

} // namespace

bool clipboardHasImage() {
    for (UINT fmt : encodedImageFormats()) {
        if (IsClipboardFormatAvailable(fmt)) return true;
    }
    return IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB) ||
           IsClipboardFormatAvailable(CF_HDROP);
}

std::optional<ClipboardImageData> readImageFromClipboard() {
    if (!openClipboardWithRetry(GetForegroundWindow())) return std::nullopt;
    std::filesystem::path dropPath;
    ClipboardImageData out;
    bool haveEncoded = false;
    bool haveBitmap = false;
    {
        struct Closer { ~Closer() { CloseClipboard(); } } closer;
        for (UINT fmt : encodedImageFormats()) {
            if (IsClipboardFormatAvailable(fmt) && readClipboardEncoded(fmt, out)) {
                haveEncoded = true;
                break;
            }
        }
        if (!haveEncoded && IsClipboardFormatAvailable(CF_HDROP)) {
            dropPath = firstClipboardDropPath();
        }
        if (!haveEncoded && dropPath.empty()) {
            if (IsClipboardFormatAvailable(CF_DIBV5) && readClipboardDIB(CF_DIBV5, out)) {
                haveBitmap = true;
            } else if (IsClipboardFormatAvailable(CF_DIB) && readClipboardDIB(CF_DIB, out)) {
                haveBitmap = true;
            }
        }
    }
    if (haveEncoded || haveBitmap) return out;
    if (!dropPath.empty() && readFileDropImage(dropPath, out)) return out;
    return std::nullopt;
}

#else // !geode_is_windows

bool copyRGBAToClipboard(uint8_t const* /*rgba*/, int /*width*/, int /*height*/) {
    return false;
}

bool clipboardHasImage() {
    return false;
}

std::optional<ClipboardImageData> readImageFromClipboard() {
    return std::nullopt;
}

#endif

}
