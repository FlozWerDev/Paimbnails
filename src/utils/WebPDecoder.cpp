#include "WebPDecoder.hpp"

#include "FormatDetect.hpp"

#include <algorithm>
#include <cstring>

#if defined(PAIMON_HAS_LIBWEBP)
#include <webp/decode.h>
#include <webp/demux.h>
#endif

namespace {

constexpr int kDefaultAnimatedFrameCap = 120;
constexpr std::uint64_t kMaxTotalDecodedPixels = 64ull * 1024 * 1024;

} // namespace

bool WebPDecoder::isWebP(std::uint8_t const* data, std::size_t size) {
    return paimon::format::isWebp(data, size);
}

bool WebPDecoder::getDimensions(
    std::uint8_t const* data,
    std::size_t size,
    int& width,
    int& height,
    bool& isAnimated
) {
    width = 0;
    height = 0;
    isAnimated = false;
    if (!isWebP(data, size)) return false;

#if defined(PAIMON_HAS_LIBWEBP)
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) return false;
    if (features.width <= 0 || features.height <= 0) return false;
    width = features.width;
    height = features.height;
    isAnimated = features.has_animation != 0;
    return true;
#else
    return paimon::format::probeWebpDimensions(data, size, width, height);
#endif
}

WebPDecoder::WebPData WebPDecoder::decode(
    std::uint8_t const* data,
    std::size_t size,
    int maxFrames,
    int maxDimension
) {
    WebPData result;
    int width = 0;
    int height = 0;
    bool animated = false;
    if (!getDimensions(data, size, width, height, animated)) return result;
    if (width <= 0 || height <= 0 || width > maxDimension || height > maxDimension) {
        return result;
    }
    std::uint64_t const framePixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (framePixels == 0 || framePixels > kMaxTotalDecodedPixels) return result;

#if defined(PAIMON_HAS_LIBWEBP)
    std::size_t const frameBytes = static_cast<std::size_t>(framePixels) * 4;
    if (!animated || maxFrames == 1) {
        std::vector<std::uint8_t> rgba(frameBytes);
        if (!WebPDecodeRGBAInto(data, size, rgba.data(), rgba.size(), width * 4)) {
            return result;
        }
        result.width = width;
        result.height = height;
        result.isAnimated = animated;
        result.frames.push_back({0, std::move(rgba)});
        return result;
    }

    ::WebPData raw{data, size};
    WebPAnimDecoderOptions decOptions;
    if (!WebPAnimDecoderOptionsInit(&decOptions)) return result;
    decOptions.color_mode = MODE_RGBA;
    decOptions.use_threads = 0;

    WebPAnimDecoder* decoder = WebPAnimDecoderNew(&raw, &decOptions);
    if (!decoder) return result;

    WebPAnimInfo info{};
    if (!WebPAnimDecoderGetInfo(decoder, &info) ||
        info.canvas_width == 0 || info.canvas_height == 0 ||
        static_cast<int>(info.canvas_width) > maxDimension ||
        static_cast<int>(info.canvas_height) > maxDimension) {
        WebPAnimDecoderDelete(decoder);
        return result;
    }

    int const canvasW = static_cast<int>(info.canvas_width);
    int const canvasH = static_cast<int>(info.canvas_height);
    std::uint64_t const canvasPixels =
        static_cast<std::uint64_t>(canvasW) * static_cast<std::uint64_t>(canvasH);
    if (canvasPixels == 0 || canvasPixels > kMaxTotalDecodedPixels) {
        WebPAnimDecoderDelete(decoder);
        return result;
    }

    int const limit = maxFrames > 0
        ? std::min(maxFrames, kDefaultAnimatedFrameCap)
        : kDefaultAnimatedFrameCap;
    std::size_t const canvasBytes = static_cast<std::size_t>(canvasPixels) * 4;

    result.width = canvasW;
    result.height = canvasH;
    result.isAnimated = info.frame_count > 1;
    result.frames.reserve(std::min<std::size_t>(info.frame_count, static_cast<std::size_t>(limit)));

    int prevTimestamp = 0;
    while (WebPAnimDecoderHasMoreFrames(decoder) &&
           static_cast<int>(result.frames.size()) < limit) {
        std::uint8_t* rgbaBuf = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder, &rgbaBuf, &timestamp) || !rgbaBuf) break;
        int const delayMs = std::max(timestamp - prevTimestamp, 10);
        prevTimestamp = timestamp;
        Frame frame;
        frame.delayMs = delayMs;
        frame.pixels.resize(canvasBytes);
        std::memcpy(frame.pixels.data(), rgbaBuf, canvasBytes);
        result.frames.push_back(std::move(frame));
    }

    WebPAnimDecoderDelete(decoder);
    if (result.frames.empty()) {
        result.width = 0;
        result.height = 0;
    }
#else
    (void)maxFrames;
#endif
    return result;
}
