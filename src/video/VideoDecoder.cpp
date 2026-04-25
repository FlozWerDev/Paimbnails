#include "VideoDecoder.hpp"
#include "platform/DecoderPLM.hpp"

#if defined(USE_MEDIA_FOUNDATION)
#include "platform/DecoderMF.hpp"
#endif
#if defined(USE_MEDIA_NDK)
#include "platform/DecoderNDK.hpp"
#endif
#if defined(USE_AV_FOUNDATION)
#include "platform/DecoderAVF.hpp"
#endif

#include <algorithm>
#include <Geode/loader/Log.hpp>

// pl_mpeg implementation in exactly one translation unit
#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

namespace paimon {

static bool endsWith(std::string const& str, std::string const& suffix) {
    if (suffix.size() > str.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin(),
        [](char a, char b) {
            return tolower(static_cast<unsigned char>(a)) ==
                   tolower(static_cast<unsigned char>(b));
        });
}

std::unique_ptr<IVideoDecoder> IVideoDecoder::create(const std::string& path) {
    bool isMpg = endsWith(path, ".mpg") || endsWith(path, ".mpeg");

    // ── MPEG-1 → always use pl_mpeg ──
    if (isMpg) {
        auto dec = std::make_unique<DecoderPLM>();
        if (dec->open(path)) return dec;
        geode::log::warn("pl_mpeg failed to open: {}", path);
        return nullptr;
    }

    // ── Platform-native decoder ──
#if defined(USE_MEDIA_FOUNDATION)
    {
        auto dec = std::make_unique<DecoderMF>();
        if (dec->open(path)) return dec;
        geode::log::warn("Media Foundation failed, trying pl_mpeg fallback: {}", path);
    }
#endif

#if defined(USE_MEDIA_NDK)
    {
        auto dec = std::make_unique<DecoderNDK>();
        if (dec->open(path)) return dec;
        geode::log::warn("MediaNDK failed, trying pl_mpeg fallback: {}", path);
    }
#endif

#if defined(USE_AV_FOUNDATION)
    {
        auto dec = std::make_unique<DecoderAVF>();
        if (dec->open(path)) return dec;
        geode::log::warn("AVFoundation failed, trying pl_mpeg fallback: {}", path);
    }
#endif

    // ── Final fallback: pl_mpeg (only supports MPEG-1) ──
    {
        auto dec = std::make_unique<DecoderPLM>();
        if (dec->open(path)) return dec;
        geode::log::warn("pl_mpeg fallback also failed: {}", path);
    }

    return nullptr;
}

} // namespace paimon
