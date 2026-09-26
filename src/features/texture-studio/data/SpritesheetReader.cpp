#include "SpritesheetReader.hpp"

#include "PlistParser.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {

ImageBuffer SpritesheetReader::extractFrame(ImageBuffer const& atlas, SpriteFrameInfo const& f) {
    if (f.rectW <= 0 || f.rectH <= 0) return ImageBuffer();
    if (atlas.empty()) return ImageBuffer();

    if (!f.rotated) {
        return atlas.subRect(f.rectX, f.rectY, f.rectW, f.rectH);
    }

    // rectW/H are logical size, so the atlas slot is their transpose; one CCW90 undoes the CW pack.
    auto rotated = atlas.subRect(f.rectX, f.rectY, f.rectH, f.rectW);
    rotated.rotateCCW90();
    return rotated;
}

ImageBuffer SpritesheetReader::composeLogicalFrame(ImageBuffer const& pixels,
                                                    SpriteFrameInfo const& f) {
    if (pixels.empty()) return ImageBuffer();

    int sourceW = std::max(f.sourceW, pixels.width());
    int sourceH = std::max(f.sourceH, pixels.height());
    if (sourceW == pixels.width() && sourceH == pixels.height() &&
        std::fabs(f.offsetX) < 0.01f && std::fabs(f.offsetY) < 0.01f) {
        return pixels;
    }

    // Cocos offset is +Y up from centre; ImageBuffer is top-left, hence -offsetY.
    int dstX = static_cast<int>(std::lround(
        (sourceW - pixels.width()) * 0.5f + f.offsetX));
    int dstY = static_cast<int>(std::lround(
        (sourceH - pixels.height()) * 0.5f - f.offsetY));

    ImageBuffer canvas(sourceW, sourceH);
    canvas.blitOverwrite(dstX, dstY, pixels);
    return canvas;
}

geode::Result<LoadedSpritesheet> SpritesheetReader::loadFromPaths(
    std::filesystem::path const& plistPath,
    std::filesystem::path const& pngPath) {

    GEODE_UNWRAP_INTO(auto parsed, PlistParser::parseFile(plistPath));
    std::error_code ec;
    auto size = std::filesystem::file_size(pngPath, ec);
    if (ec || size > ImageBuffer::kMaxEncodedBytes) {
        return Err("SpritesheetReader: invalid PNG file size for {}",
            geode::utils::string::pathToString(pngPath));
    }
    auto pngBytes = file::readBinary(pngPath);
    if (!pngBytes) {
        return Err("SpritesheetReader: cannot read PNG {}: {}",
            geode::utils::string::pathToString(pngPath), pngBytes.unwrapErr());
    }
    return loadFromMemory(parsed, std::span<std::uint8_t const>(
        pngBytes.unwrap().data(), pngBytes.unwrap().size()));
}

geode::Result<LoadedSpritesheet> SpritesheetReader::loadFromMemory(
    ParsedSpritesheet const& parsed,
    std::span<std::uint8_t const> pngBytes) {

    GEODE_UNWRAP_INTO(auto atlas, ImageBuffer::loadFromMemory(pngBytes));

    LoadedSpritesheet out;
    out.metadata    = parsed.metadata;
    out.atlasWidth  = atlas.width();
    out.atlasHeight = atlas.height();

    if (parsed.metadata.sizeW > 0 && parsed.metadata.sizeH > 0) {
        if (parsed.metadata.sizeW != atlas.width() || parsed.metadata.sizeH != atlas.height()) {
            log::warn("[texture-studio] plist metadata.size = ({}, {}) but PNG is ({}, {}), continuing with PNG dims",
                parsed.metadata.sizeW, parsed.metadata.sizeH, atlas.width(), atlas.height());
        }
    }

    out.frames.reserve(parsed.frames.size());
    for (auto const& f : parsed.frames) {
        ExtractedFrame ef;
        ef.info   = f;
        ef.pixels = extractFrame(atlas, f);
        if (ef.pixels.width() != ef.info.spriteW || ef.pixels.height() != ef.info.spriteH) {
            ef.info.spriteW = ef.pixels.width();
            ef.info.spriteH = ef.pixels.height();
        }
        out.frames.push_back(std::move(ef));
    }

    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
