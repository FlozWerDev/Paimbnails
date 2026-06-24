#include "SpritesheetReader.hpp"

#include "PlistParser.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::texture_studio {

ImageBuffer SpritesheetReader::extractFrame(ImageBuffer const& atlas, SpriteFrameInfo const& f) {
    // Sanity: sprites with zero-area rects (rare, but seen in malformed
    // packs) get an empty buffer.
    if (f.rectW <= 0 || f.rectH <= 0) return ImageBuffer();
    if (atlas.empty()) return ImageBuffer();

    // For non-rotated frames we just copy the rect and we're done.
    if (!f.rotated) {
        return atlas.subRect(f.rectX, f.rectY, f.rectW, f.rectH);
    }

    // Rotated case: cocos2d packs the sprite rotated 90° clockwise inside
    // the atlas. To recover the logical orientation we extract the rotated
    // rect first (its width/height in the atlas are SWAPPED compared to
    // the logical sprite size), then rotate counter-clockwise.
    //
    // PackGen's processPlistFile() does this in two steps: first translate-
    // and-rotate by 90° (which lands flipped), then rotate by 180° to
    // arrive at the correct orientation. The net is exactly a CCW90 of the
    // raw rect, which is what rotateCCW90() does.
    auto rotated = atlas.subRect(f.rectX, f.rectY, f.rectW, f.rectH);
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

    // cocos2d stores spriteOffset from the source-frame centre, with +Y up.
    // ImageBuffer uses a top-left origin, hence the minus sign for offsetY.
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

    // Sanity check between recorded size and actual size. Mismatch is not
    // fatal — some packs ship with metadata.size set to 0 — but warn.
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
        // Defensive: ensure the spriteW/H match the buffer we just produced
        // (subRect clips out-of-bounds rects to a smaller buffer, in which
        // case the metadata would be wrong).
        if (ef.pixels.width() != ef.info.spriteW || ef.pixels.height() != ef.info.spriteH) {
            // Update metadata so packer/builder agree with the actual pixels.
            ef.info.spriteW = ef.pixels.width();
            ef.info.spriteH = ef.pixels.height();
        }
        out.frames.push_back(std::move(ef));
    }

    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
