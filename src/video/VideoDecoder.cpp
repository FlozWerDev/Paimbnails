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

#include <Geode/loader/Log.hpp>

// pl_mpeg implementation in exactly one translation unit
#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

namespace paimon {

std::unique_ptr<IVideoDecoder> IVideoDecoder::create(const std::string& path) {
    // Native backend first for every container; pl_mpeg only understands
    // MPEG-1 program streams, so it stays last as a fallback.
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

    // Final fallback: pl_mpeg (only supports MPEG-1)
    {
        auto dec = std::make_unique<DecoderPLM>();
        if (dec->open(path)) return dec;
        geode::log::warn("pl_mpeg fallback also failed: {}", path);
    }

    return nullptr;
}

} // namespace paimon
