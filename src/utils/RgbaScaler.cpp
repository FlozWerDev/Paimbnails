#include "RgbaScaler.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include <libyuv/scale_argb.h>

namespace paimon::rgba {
namespace {

bool validDimensions(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    auto const pixels = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    return pixels <= std::numeric_limits<size_t>::max() / 4;
}

void fallbackScale(
    uint8_t const* src,
    int srcWidth,
    int srcHeight,
    uint8_t* dst,
    int dstWidth,
    int dstHeight
) {
    for (int dy = 0; dy < dstHeight; ++dy) {
        int sy0 = static_cast<int>(static_cast<int64_t>(dy) * srcHeight / dstHeight);
        int sy1 = static_cast<int>(static_cast<int64_t>(dy + 1) * srcHeight / dstHeight);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        sy1 = std::min(sy1, srcHeight);

        for (int dx = 0; dx < dstWidth; ++dx) {
            int sx0 = static_cast<int>(static_cast<int64_t>(dx) * srcWidth / dstWidth);
            int sx1 = static_cast<int>(static_cast<int64_t>(dx + 1) * srcWidth / dstWidth);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            sx1 = std::min(sx1, srcWidth);

            uint64_t channels[4] = {};
            uint64_t samples = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                auto const* row = src + (static_cast<size_t>(sy) * srcWidth + sx0) * 4;
                for (int sx = sx0; sx < sx1; ++sx) {
                    for (int channel = 0; channel < 4; ++channel) {
                        channels[channel] += row[channel];
                    }
                    row += 4;
                    ++samples;
                }
            }

            auto* pixel = dst + (static_cast<size_t>(dy) * dstWidth + dx) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                pixel[channel] = static_cast<uint8_t>(channels[channel] / std::max<uint64_t>(1, samples));
            }
        }
    }
}

} // namespace

std::unique_ptr<uint8_t[]> scale(
    uint8_t const* src,
    int srcWidth,
    int srcHeight,
    int dstWidth,
    int dstHeight
) {
    if (!src || !validDimensions(srcWidth, srcHeight)
        || !validDimensions(dstWidth, dstHeight)) {
        return nullptr;
    }

    auto result = std::make_unique<uint8_t[]>(
        static_cast<size_t>(dstWidth) * static_cast<size_t>(dstHeight) * 4);

    if (srcWidth == dstWidth && srcHeight == dstHeight) {
        std::memcpy(result.get(), src,
                    static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight) * 4);
        return result;
    }

    // ARGBScale treats channels independently: exactly right for packed RGBA8888.
    auto const filter = (dstWidth < srcWidth || dstHeight < srcHeight)
        ? libyuv::kFilterBox
        : libyuv::kFilterBilinear;
    int const status = libyuv::ARGBScale(
        src,
        srcWidth * 4,
        srcWidth,
        srcHeight,
        result.get(),
        dstWidth * 4,
        dstWidth,
        dstHeight,
        filter
    );
    if (status == 0) return result;

    fallbackScale(src, srcWidth, srcHeight, result.get(), dstWidth, dstHeight);
    return result;
}

} // namespace paimon::rgba
