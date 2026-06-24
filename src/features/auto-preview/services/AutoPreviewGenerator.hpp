#pragma once

#include <cstdint>
#include <memory>

namespace paimon::autopreview {

std::unique_ptr<uint8_t[]> downscaleRGBA(
    uint8_t const* src, int srcW, int srcH, int dstW, int dstH);

void storeCapturedFrame(
    int32_t levelID, std::shared_ptr<uint8_t> rgba, int width, int height);

} // namespace paimon::autopreview
