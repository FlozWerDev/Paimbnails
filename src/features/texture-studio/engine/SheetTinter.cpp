#include "SheetTinter.hpp"
#include <Geode/utils/string.hpp>

#include "../data/PlistBuilder.hpp"
#include "../data/RectPacker.hpp"
#include "../data/SpritesheetReader.hpp"
#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "LuminanceTinter.hpp"
#include "MaskBuilder.hpp"
#include "SpritePreviewRenderer.hpp"
#include "UiSpriteCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Bilinear-ish downscale by an arbitrary scale factor. We don't need
// production-grade quality (the user is generating texture packs, not
// printing posters), but the result has to look smooth enough that an
// HD port from a UHD source isn't visibly blocky.
//
// For scale = 0.5 we use a 2×2 box average (ideal for half-resolution).
// For other scales we fall back to nearest-neighbour. Practically only
// 0.5 is used (UHD → HD), so this is fine.
ImageBuffer resizeImage(ImageBuffer const& src, float scale) {
    if (scale <= 0.0f || scale == 1.0f || src.empty()) return src;
    int newW = std::max(1, static_cast<int>(std::floor(src.width()  * scale)));
    int newH = std::max(1, static_cast<int>(std::floor(src.height() * scale)));
    ImageBuffer out(newW, newH);

    if (std::fabs(scale - 0.5f) < 1e-3f) {
        // 2×2 box filter — averages 4 source pixels per dest pixel.
        for (int y = 0; y < newH; ++y) {
            for (int x = 0; x < newW; ++x) {
                int sx = x * 2;
                int sy = y * 2;
                int r = 0, g = 0, b = 0, a = 0;
                int n = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        int xx = sx + dx;
                        int yy = sy + dy;
                        if (xx < src.width() && yy < src.height()) {
                            auto p = src.atRef(xx, yy);
                            r += p[0]; g += p[1]; b += p[2]; a += p[3];
                            ++n;
                        }
                    }
                }
                if (n > 0) {
                    out.setAt(x, y, {
                        static_cast<std::uint8_t>(r / n),
                        static_cast<std::uint8_t>(g / n),
                        static_cast<std::uint8_t>(b / n),
                        static_cast<std::uint8_t>(a / n),
                    });
                }
            }
        }
    } else {
        // Nearest-neighbour fallback.
        float invS = 1.0f / scale;
        for (int y = 0; y < newH; ++y) {
            for (int x = 0; x < newW; ++x) {
                int sx = std::min(src.width()  - 1, static_cast<int>(x * invS));
                int sy = std::min(src.height() - 1, static_cast<int>(y * invS));
                auto p = src.atRef(sx, sy);
                out.setAt(x, y, {p[0], p[1], p[2], p[3]});
            }
        }
    }
    return out;
}

}  // anonymous namespace

geode::Result<SheetTinterOutput> SheetTinter::process(SheetTinterRequest const& req) {
    // Step 1: load the sheet + extract frames.
    GEODE_UNWRAP_INTO(auto loaded, SpritesheetReader::loadFromPaths(req.sourcePlist, req.sourcePng));
    if (loaded.frames.empty()) {
        return Err("SheetTinter: sheet '{}' contains no frames", req.outputBaseName);
    }

    // Step 2: process each frame. We replace ExtractedFrame::pixels with
    // the tinted (and optionally resized) image, and update the metadata
    // so the packer/builder agree with the new sizes.
    int needsReviewCount = 0;

    struct Tinted {
        std::string  name;
        ImageBuffer  pixels;
        SpriteFrameInfo info;  // updated metadata (size, offset)
    };
    std::vector<Tinted> tinted;
    tinted.reserve(loaded.frames.size());

    int tintedCount = 0;

    for (auto& frame : loaded.frames) {
        auto& origPixels = frame.pixels;
        if (origPixels.empty()) {
            // Skip zero-size frames (shouldn't happen but be defensive).
            continue;
        }

        // Filtro de UI + overrides por sprite:
        // 1. spriteSkip siempre gana — el sprite pasa intacto.
        // 2. Un override de color explícito tiñe aunque no sea UI.
        // 3. Si no, aplica el filtro global de UI.
        auto colorsIt = req.spriteColors.find(frame.info.name);
        auto imageIt = req.spriteImages.find(frame.info.name);
        bool hasColorOverride = (colorsIt != req.spriteColors.end());
        bool hasImageOverride = (imageIt != req.spriteImages.end());
        auto kind = UiSpriteCatalog::classify(frame.info.name, req.outputBaseName);
        bool tintThisFrame =
            req.spriteSkip.find(frame.info.name) == req.spriteSkip.end()
            && (hasColorOverride
                || !req.onlyTintUiSprites
                || UiSpriteCatalog::shouldTint(kind, req.tintScope));

        ImageBuffer recolored;
        if (hasImageOverride &&
            req.spriteSkip.find(frame.info.name) == req.spriteSkip.end()) {
            auto custom = ImageBuffer::loadFromFile(imageIt->second);
            if (custom) {
                recolored = SpritePreviewRenderer::renderCustomImage(
                    custom.unwrap(), origPixels.width(), origPixels.height());
                ++tintedCount;
            } else {
                log::warn("[texture-studio] custom image '{}' unavailable: {}",
                    geode::utils::string::pathToString(imageIt->second), custom.unwrapErr());
                recolored = origPixels;
            }
        } else if (tintThisFrame) {
            // Cluster + classify + masks + tint.
            auto clusters    = ColorClustering::compute(origPixels);
            auto classified  = ClusterClassifier::classify(clusters, origPixels);
            if (classified.needsReview) ++needsReviewCount;

            MaskBuilderOptions mopts;
            mopts.softness = req.maskSoftness;
            auto masks = MaskBuilder::build(origPixels, classified, mopts);

            TinterOptions topts;
            topts.brightness = req.brightness;
            topts.alternativeGlowOverlay = req.alternativeGlowOverlay;
            TintColors const& frameColors =
                hasColorOverride ? colorsIt->second : req.colors;
            recolored = LuminanceTinter::apply(origPixels, masks, frameColors, topts);
            ++tintedCount;
        } else {
            // Passthrough: copia intacta del sprite original.
            recolored = origPixels;
        }

        // Resize for medium port if requested.
        if (req.resizeScale != 1.0f) {
            recolored = resizeImage(recolored, req.resizeScale);
        }

        Tinted t;
        t.name   = frame.info.name;
        t.pixels = std::move(recolored);
        t.info   = frame.info;

        // Update metadata to reflect the (possibly downscaled) size.
        // IMPORTANT: spriteSourceSize is NOT the same as spriteSize.
        //   spriteSize       = un-rotated logical pixel dimensions (post-trim)
        //   spriteSourceSize = full pre-trim sprite dimensions (often larger)
        //   spriteOffset     = how much the trimmed sprite is shifted within
        //                      its source rect. Lives in (sourceW, sourceH)
        //                      space, NOT in (spriteW, spriteH) space.
        //
        // GD relies on (sourceSize, offset) to correctly position sprites in
        // the world. If we collapse sourceSize to spriteSize, sprites that
        // were originally trimmed (e.g. cogwheel_320x320 trimmed to 162x162)
        // get mis-positioned, which breaks layouts and ultimately leads to
        // dangling frame pointers when the cocos2d scene tree cleans up.
        // That is the root cause of the CCNode::cleanup crash on scene
        // transitions after applying the generated pack.
        //
        // We update spriteW/H to the post-tint pixel size, but preserve
        // sourceW/H from the original plist (scaled if we are in a medium
        // port pass).
        int origSourceW = (frame.info.sourceW > 0) ? frame.info.sourceW : frame.info.spriteW;
        int origSourceH = (frame.info.sourceH > 0) ? frame.info.sourceH : frame.info.spriteH;

        t.info.spriteW = t.pixels.width();
        t.info.spriteH = t.pixels.height();

        if (req.resizeScale != 1.0f && req.resizeScale > 0.0f) {
            // Scale the source size by the same factor so (sourceSize,
            // offset) stays consistent with the new spriteSize.
            t.info.sourceW = std::max(1,
                static_cast<int>(std::lround(origSourceW * req.resizeScale)));
            t.info.sourceH = std::max(1,
                static_cast<int>(std::lround(origSourceH * req.resizeScale)));
        } else {
            // No resize: preserve the source size verbatim. spriteOffset
            // stays unchanged because we did not trim anything new.
            t.info.sourceW = origSourceW;
            t.info.sourceH = origSourceH;
        }

        // Defensive: spriteSize must never exceed sourceSize. If somehow it
        // does (e.g. a pack with bogus metadata), pin sourceSize up so the
        // engine doesn't reject the frame.
        if (t.info.sourceW < t.info.spriteW) t.info.sourceW = t.info.spriteW;
        if (t.info.sourceH < t.info.spriteH) t.info.sourceH = t.info.spriteH;

        // Offsets must scale by the same factor — except for PackGen's
        // hard-coded GJ_table_side_001 case (otherwise the table breaks
        // visually in medium-quality packs).
        bool preserveOffset =
            req.preserveOffsetForTableSide &&
            t.name.find("GJ_table_side_001") != std::string::npos;
        if (!preserveOffset && req.resizeScale != 1.0f && req.resizeScale > 0.0f) {
            t.info.offsetX *= req.resizeScale;
            t.info.offsetY *= req.resizeScale;
        }

        // After tinting we always emit non-rotated frames in the new atlas
        // (matches PackGen behaviour — simpler atlases, no engine cost).
        t.info.rotated = false;

        tinted.push_back(std::move(t));
    }

    if (tinted.empty()) {
        return Err("SheetTinter: no usable frames produced for '{}'", req.outputBaseName);
    }

    // Step 3: pack with shelf algorithm. We feed the inputs sorted by
    // descending height already (RectPacker re-sorts internally so this is
    // for output-stability only).
    std::vector<RectPackInput> packerInput;
    packerInput.reserve(tinted.size());
    for (auto const& t : tinted) {
        RectPackInput r;
        r.id     = t.name;
        r.width  = t.pixels.width();
        r.height = t.pixels.height();
        packerInput.push_back(r);
    }
    auto pack = RectPacker::pack(std::move(packerInput));
    if (pack.sheetWidth <= 0 || pack.sheetHeight <= 0) {
        return Err("SheetTinter: packer produced empty atlas for '{}'", req.outputBaseName);
    }

    // Step 4: build the output atlas image. We blit each tinted frame at
    // its placement.
    ImageBuffer atlas(pack.sheetWidth, pack.sheetHeight);

    // Index the tinted frames by name for O(1) lookup during placement.
    // (Vector lookup would be O(n), and with 1500+ frames per sheet that
    // matters in the inner loop.)
    std::unordered_map<std::string, Tinted const*> byName;
    byName.reserve(tinted.size());
    for (auto const& t : tinted) byName.emplace(t.name, &t);

    // Build the output frame list in placement order so the plist mirrors
    // the visual layout of the atlas.
    std::vector<SpriteFrameInfo> outFrames;
    outFrames.reserve(pack.placements.size());

    for (auto const& p : pack.placements) {
        auto it = byName.find(p.id);
        if (it == byName.end()) continue;  // shouldn't happen
        auto const* t = it->second;
        atlas.blitOverwrite(p.x, p.y, t->pixels);

        SpriteFrameInfo info = t->info;
        info.name   = p.id;
        info.rectX  = p.x;
        info.rectY  = p.y;
        info.rectW  = p.w;
        info.rectH  = p.h;
        info.rotated = false;
        outFrames.push_back(std::move(info));
    }

    // Step 5: encode PNG + build plist string.
    auto pngRes = atlas.encodeAsPng();
    if (!pngRes) {
        return Err("SheetTinter: PNG encode failed for '{}': {}",
            req.outputBaseName, pngRes.unwrapErr());
    }

    // Construct the output plist metadata. PackGen replaces "-uhd" with
    // "-hd" in textureFileName when emitting the medium port; we follow.
    PlistMetadata meta = loaded.metadata;
    meta.format     = 3;
    meta.sizeW      = pack.sheetWidth;
    meta.sizeH      = pack.sheetHeight;
    meta.textureFileName     = req.outputBaseName + req.outputQualitySuffix + ".png";
    meta.realTextureFileName = meta.textureFileName;
    // GD emits sprite sheets with premultiplyAlpha=false (the PNG holds
    // straight RGBA bytes; cocos2d does the premultiply at upload time).
    // Our PNGs from stb_image_write are also straight RGBA, so we MUST
    // declare false here. Declaring true would make cocos2d skip the
    // straight->premul conversion at upload, producing washed-out colors
    // and incorrect alpha-blend during scene composition.
    meta.premultiplyAlpha    = false;

    ParsedSpritesheet outSheet;
    outSheet.metadata = meta;
    outSheet.frames   = std::move(outFrames);

    auto plistRes = PlistBuilder::buildString(outSheet);
    if (!plistRes) {
        return Err("SheetTinter: plist build failed for '{}': {}",
            req.outputBaseName, plistRes.unwrapErr());
    }

    SheetTinterOutput out;
    out.pngBytes     = std::move(pngRes).unwrap();
    out.plistXml     = std::move(plistRes).unwrap();
    out.atlasWidth   = pack.sheetWidth;
    out.atlasHeight  = pack.sheetHeight;
    out.frameCount   = static_cast<int>(tinted.size());
    out.needsReviewCnt = needsReviewCount;
    out.tintedFrameCount = tintedCount;
    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
