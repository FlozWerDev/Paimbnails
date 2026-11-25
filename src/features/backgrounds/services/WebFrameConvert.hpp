#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define PAIMBNAILS_HAVE_SSE2_FRAME_CONVERT 1
#include <emmintrin.h>
#endif

namespace paimon::wallpaper {

// Windows Graphics Capture hands back BGRA rows while Cocos uploads RGBA.
// the captured window is always opaque, so alpha is forced to 0xFF instead of
// trusting the channel. Vectorised on x64: a 1600x900 frame runs 30 times per
// second, so the scalar per-pixel loop used to be measurable CPU on the
// capture worker. dst and src may be unaligned.
inline void convertBgraToRgba(std::uint8_t* dst, std::uint8_t const* src, std::size_t pixels) {
    std::size_t i = 0;
#ifdef PAIMBNAILS_HAVE_SSE2_FRAME_CONVERT
    // swap the R and B bytes inside each 32-bit lane, keep G, force A to 255.
    if (pixels >= 4) {
        __m128i const maskRb = _mm_set1_epi32(0x00FF00FF);
        __m128i const maskGa = _mm_set1_epi32(0xFF00FF00);
        __m128i const alpha = _mm_set1_epi32(0xFF000000);
        for (; i + 4 <= pixels; i += 4) {
            __m128i const value = _mm_loadu_si128(reinterpret_cast<__m128i const*>(src + i * 4));
            __m128i channels = _mm_and_si128(value, maskRb);
            channels = _mm_or_si128(_mm_srli_epi32(channels, 16), _mm_slli_epi32(channels, 16));
            __m128i const result = _mm_or_si128(_mm_or_si128(_mm_and_si128(value, maskGa), channels), alpha);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i * 4), result);
        }
    }
#endif
    for (; i < pixels; ++i) {
        dst[i * 4 + 0] = src[i * 4 + 2];
        dst[i * 4 + 1] = src[i * 4 + 1];
        dst[i * 4 + 2] = src[i * 4 + 0];
        dst[i * 4 + 3] = 0xFF;
    }
}

} // namespace paimon::wallpaper
