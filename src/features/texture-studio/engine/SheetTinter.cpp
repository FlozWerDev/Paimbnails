#include "SheetTinter.hpp"
#include <Geode/utils/string.hpp>

#include "../data/PlistBuilder.hpp"
#include "../data/PlistParser.hpp"
#include "../data/RectPacker.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../persist/FusionStore.hpp"
#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "FusionAsset.hpp"
#include "FusionEngine.hpp"
#include "LuminanceTinter.hpp"
#include "MaskBuilder.hpp"
#include "OverlayTinter.hpp"
#include "SpritePreviewRenderer.hpp"
#include "UiSpriteCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Downscale by scale; 0.5 uses a 2×2 box average, others use nearest-neighbor.
ImageBuffer resizeImage(ImageBuffer const& src, float scale) {
    if (scale <= 0.0f || scale == 1.0f || src.empty()) return src;
    int newW = std::max(1, static_cast<int>(std::floor(src.width()  * scale)));
    int newH = std::max(1, static_cast<int>(std::floor(src.height() * scale)));
    ImageBuffer out(newW, newH);

    if (std::fabs(scale - 0.5f) < 1e-3f) {
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

TinterOptions makeTintOptions(SheetTinterRequest const& req) {
    TinterOptions topts;
    topts.brightness             = req.brightness;
    topts.alternativeGlowOverlay = req.alternativeGlowOverlay;
    topts.darkOutlineThreshold   = std::clamp(req.outlineProtect, 0, 255);
    topts.saturation             = req.saturation;
    topts.contrast               = req.contrast;
    return topts;
}

// Auto-detected tint for one logical frame.
ImageBuffer clusterTintFrame(ImageBuffer const& logical, SheetTinterRequest const& req,
                             TintColors const& colors, int& needsReviewCount) {
    ClusteringOptions copts;
    copts.k = std::clamp(req.clusterPrecision, 2, 10);
    auto clusters   = ColorClustering::compute(logical, copts);
    auto classified = ClusterClassifier::classify(clusters, logical);
    if (classified.needsReview) ++needsReviewCount;

    MaskBuilderOptions mopts;
    mopts.softness   = req.maskSoftness;
    mopts.edgeRefine = std::clamp(req.edgeCleanup, 0, 4);
    auto masks = MaskBuilder::build(logical, classified, mopts);

    return LuminanceTinter::apply(logical, masks, colors, makeTintOptions(req));
}

// Render an override into a frame-sized canvas, or return an empty buffer.
ImageBuffer loadCustomCanvas(SheetTinterRequest const& req, std::string const& frameName,
                             int frameW, int frameH, bool& overlayMode) {
    overlayMode = false;
    auto it = req.spriteImages.find(frameName);
    if (it == req.spriteImages.end()) return ImageBuffer();
    if (req.spriteSkip.find(frameName) != req.spriteSkip.end()) return ImageBuffer();

    auto custom = ImageBuffer::loadFromFile(it->second.path);
    if (!custom) {
        log::warn("[texture-studio] custom image '{}' unavailable: {}",
            geode::utils::string::pathToString(it->second.path), custom.unwrapErr());
        return ImageBuffer();
    }
    overlayMode = it->second.overlay;
    return SpritePreviewRenderer::renderCustomImage(
        custom.unwrap(), frameW, frameH, it->second.transform);
}

// Apply a stored fusion in place; export uses the first GIF frame as static PNG.
bool applyFusionIfAny(SheetTinterRequest const& req, std::string const& frameName,
                      ImageBuffer& frame) {
    if (frame.empty()) return false;
    if (req.spriteSkip.find(frameName) != req.spriteSkip.end()) return false;
    auto it = req.spriteFusions.find(frameName);
    if (it == req.spriteFusions.end()) return false;

    auto maskRes = FusionStore::load(it->second.maskPath);
    if (!maskRes) {
        log::warn("[texture-studio] fusion mask '{}': {}",
            geode::utils::string::pathToString(it->second.maskPath),
            maskRes.unwrapErr());
        return false;
    }
    auto payload = std::move(maskRes).unwrap();
    if (payload.mask.width != frame.width() || payload.mask.height != frame.height()) {
        log::warn("[texture-studio] fusion mask size {}x{} != frame {}x{} for '{}'",
            payload.mask.width, payload.mask.height,
            frame.width(), frame.height(), frameName);
        return false;
    }

    auto texRes = FusionAssetLoader::loadStaticFrame(it->second.texturePath);
    if (!texRes) {
        log::warn("[texture-studio] fusion texture '{}': {}",
            geode::utils::string::pathToString(it->second.texturePath),
            texRes.unwrapErr());
        return false;
    }

    FusionApplyOptions opts;
// Stamp the texture as-is; pack colors do not recolor it.
    opts.blendMode = it->second.blendMode;
    opts.opacity   = it->second.opacity > 0.f ? it->second.opacity : payload.opacity;
    opts.transform = it->second.transform.isDefault()
        ? payload.transform : it->second.transform;
    opts.pixelOffsetX = it->second.pixelOffsetX;
    opts.pixelOffsetY = it->second.pixelOffsetY;
    FusionEngine::apply(frame, payload.mask, texRes.unwrap(), opts);
    return true;
}

// Reinsert a logical frame at its atlas slot, restoring Cocos' packing rotation.
void blitLogicalBack(ImageBuffer& atlas, SpriteFrameInfo const& f, ImageBuffer logical) {
    if (logical.empty()) return;
    if (f.rotated) logical.rotateCW90();
    atlas.blitOverwrite(f.rectX, f.rectY, logical);
}

std::string outputTextureName(SheetTinterRequest const& req) {
    std::string texName = req.outputBaseName + req.outputQualitySuffix + ".png";
    if (auto slash = texName.find_last_of('/'); slash != std::string::npos) {
        texName = texName.substr(slash + 1);
    }
    return texName;
}

geode::Result<SheetTinterOutput> buildOutput(ImageBuffer const& atlas,
                                             ParsedSpritesheet const& outSheet,
                                             SheetTinterRequest const& req,
                                             int frameCount, int tintedCount,
                                             int needsReviewCount) {
    auto pngRes = atlas.encodeAsPng();
    if (!pngRes) {
        return Err("SheetTinter: PNG encode failed for '{}': {}",
            req.outputBaseName, pngRes.unwrapErr());
    }
    auto plistRes = PlistBuilder::buildString(outSheet);
    if (!plistRes) {
        return Err("SheetTinter: plist build failed for '{}': {}",
            req.outputBaseName, plistRes.unwrapErr());
    }

    SheetTinterOutput out;
    out.pngBytes         = std::move(pngRes).unwrap();
    out.plistXml         = std::move(plistRes).unwrap();
    out.atlasWidth       = atlas.width();
    out.atlasHeight      = atlas.height();
    out.frameCount       = frameCount;
    out.needsReviewCnt   = needsReviewCount;
    out.tintedFrameCount = tintedCount;
    return Ok(std::move(out));
}

// UHD: tint in place, preserve atlas layout, and emit the plist unchanged.
geode::Result<SheetTinterOutput> processInPlace(SheetTinterRequest const& req,
                                                ParsedSpritesheet const& parsed,
                                                ImageBuffer const& atlas) {
    ImageBuffer out(atlas);  // working copy

    bool overlayPath = req.overlaySources && req.overlaySources->any();
    int tintedCount = 0;
    int needsReviewCount = 0;

    // Whole-sheet overlay covers every frame at once.
    if (overlayPath) {
        auto const& s = *req.overlaySources;
        OverlayImages ov;
        ov.overlay1 = s.overlay1;
        ov.overlay2 = s.overlay2;
        ov.gold     = s.gold;
        ov.demon1   = s.demon1;
        ov.demon2   = s.demon2;
        ov.glow     = s.glow;
        if (ov.anyUsable(out.width(), out.height())) {
            out = OverlayTinter::apply(out, ov, req.colors, makeTintOptions(req));
        }
    }

    for (auto const& info : parsed.frames) {
        bool skip     = req.spriteSkip.find(info.name) != req.spriteSkip.end();
        auto colorsIt = req.spriteColors.find(info.name);
        bool hasColor = colorsIt != req.spriteColors.end();
        bool hasImage = req.spriteImages.find(info.name) != req.spriteImages.end();
        bool hasFusion = req.spriteFusions.find(info.name) != req.spriteFusions.end();

    // Per-frame work is limited to explicit overrides or tint reversals.
        if (overlayPath && !skip && !hasColor && !hasImage && !hasFusion) continue;

        ImageBuffer orig = SpritesheetReader::extractFrame(atlas, info);
        if (orig.empty()) continue;

        if (skip) {
            if (overlayPath) blitLogicalBack(out, info, orig);
            continue;
        }

        bool imageOverlay = false;
        ImageBuffer customCanvas =
            loadCustomCanvas(req, info.name, orig.width(), orig.height(), imageOverlay);

        ImageBuffer resultFrame;
        if (!customCanvas.empty() && !imageOverlay) {
            resultFrame = std::move(customCanvas);
            ++tintedCount;
        } else if (hasColor) {
    // Explicit per-sprite colors override the sheet tint.
            resultFrame = clusterTintFrame(orig, req, colorsIt->second, needsReviewCount);
            ++tintedCount;
            if (!customCanvas.empty()) SpritePreviewRenderer::compositeOver(resultFrame, customCanvas);
        } else if (overlayPath) {
    // Composite custom images over the tinted region.
            resultFrame = SpritesheetReader::extractFrame(out, info);
            if (!customCanvas.empty()) SpritePreviewRenderer::compositeOver(resultFrame, customCanvas);
        } else {
    // Auto-detection fallback when no overlay pack is available.
            auto kind = UiSpriteCatalog::classify(info.name, req.outputBaseName);
            bool tintThis = !req.onlyTintUiSprites
                         || UiSpriteCatalog::shouldTint(kind, req.tintScope);
            if (tintThis) {
                resultFrame = clusterTintFrame(orig, req, req.colors, needsReviewCount);
                ++tintedCount;
            } else {
                resultFrame = orig;
            }
            if (!customCanvas.empty()) {
                SpritePreviewRenderer::compositeOver(resultFrame, customCanvas);
                ++tintedCount;
            }
        }

        if (applyFusionIfAny(req, info.name, resultFrame)) {
            ++tintedCount;
        }

        blitLogicalBack(out, info, std::move(resultFrame));
    }

    // Preserve original frame layout; normalize metadata only.
    ParsedSpritesheet outSheet;
    outSheet.metadata = parsed.metadata;
    outSheet.metadata.format              = 3;
    outSheet.metadata.sizeW               = out.width();
    outSheet.metadata.sizeH               = out.height();
    outSheet.metadata.textureFileName     = outputTextureName(req);
    outSheet.metadata.realTextureFileName = outSheet.metadata.textureFileName;
    // PNGs use straight RGBA, like GD's sheets.
    outSheet.metadata.premultiplyAlpha    = false;
    outSheet.frames = parsed.frames;

    int frameCount = static_cast<int>(parsed.frames.size());
    return buildOutput(out, outSheet, req,
                       frameCount,
                       overlayPath ? frameCount : tintedCount,
                       needsReviewCount);
}

// HD port: downscale and re-pack each frame; only the optional half-res copy changes layout.
geode::Result<SheetTinterOutput> processRepack(SheetTinterRequest const& req,
                                               ParsedSpritesheet const& parsed,
                                               ImageBuffer const& atlas) {
    int needsReviewCount = 0;
    int tintedCount = 0;

    struct Tinted {
        std::string     name;
        ImageBuffer     pixels;
        SpriteFrameInfo info;
    };
    std::vector<Tinted> tinted;
    tinted.reserve(parsed.frames.size());

    for (auto const& info : parsed.frames) {
        ImageBuffer origPixels = SpritesheetReader::extractFrame(atlas, info);
        if (origPixels.empty()) continue;

        auto colorsIt = req.spriteColors.find(info.name);
        bool hasColorOverride = colorsIt != req.spriteColors.end();
        bool skipped = req.spriteSkip.find(info.name) != req.spriteSkip.end();
        auto kind = UiSpriteCatalog::classify(info.name, req.outputBaseName);
        bool tintThisFrame =
            !skipped
            && (hasColorOverride
                || !req.onlyTintUiSprites
                || UiSpriteCatalog::shouldTint(kind, req.tintScope));

        bool useOverlayPath =
            req.overlaySources && req.overlaySources->any()
            && !skipped && !hasColorOverride;

        bool imageOverlay = false;
        ImageBuffer customCanvas =
            loadCustomCanvas(req, info.name, origPixels.width(), origPixels.height(), imageOverlay);

        ImageBuffer recolored;
        if (!customCanvas.empty() && !imageOverlay) {
            recolored = std::move(customCanvas);
            ++tintedCount;
        } else if (useOverlayPath) {
            auto const& src = *req.overlaySources;
            auto crop = [&](ImageBuffer const& sheet) {
                return sheet.empty()
                    ? ImageBuffer()
                    : SpritesheetReader::extractFrame(sheet, info);
            };
            OverlayImages ov;
            ov.overlay1 = crop(src.overlay1);
            ov.overlay2 = crop(src.overlay2);
            ov.gold     = crop(src.gold);
            ov.demon1   = crop(src.demon1);
            ov.demon2   = crop(src.demon2);
            ov.glow     = crop(src.glow);

            auto hasInk = [](ImageBuffer const& img) {
                auto const* p = img.data();
                for (std::size_t i = 0, n = img.pixelCount(); i < n; ++i) {
                    if (p[i * 4 + 3] != 0) return true;
                }
                return false;
            };
            bool anyInk = hasInk(ov.overlay1) || hasInk(ov.overlay2)
                       || hasInk(ov.gold)     || hasInk(ov.demon1)
                       || hasInk(ov.demon2)   || hasInk(ov.glow);

            if (anyInk && ov.anyUsable(origPixels.width(), origPixels.height())) {
                recolored = OverlayTinter::apply(origPixels, ov, req.colors, makeTintOptions(req));
                ++tintedCount;
            } else {
                recolored = origPixels;
            }
            if (!customCanvas.empty()) SpritePreviewRenderer::compositeOver(recolored, customCanvas);
        } else if (tintThisFrame) {
            TintColors const& frameColors =
                hasColorOverride ? colorsIt->second : req.colors;
            recolored = clusterTintFrame(origPixels, req, frameColors, needsReviewCount);
            if (!customCanvas.empty()) SpritePreviewRenderer::compositeOver(recolored, customCanvas);
            ++tintedCount;
        } else {
            recolored = origPixels;
            if (!customCanvas.empty()) {
                SpritePreviewRenderer::compositeOver(recolored, customCanvas);
                ++tintedCount;
            }
        }

        if (applyFusionIfAny(req, info.name, recolored)) {
            ++tintedCount;
        }

        recolored = resizeImage(recolored, req.resizeScale);

        Tinted t;
        t.name   = info.name;
        t.info   = info;

        int origSourceW = (info.sourceW > 0) ? info.sourceW : info.spriteW;
        int origSourceH = (info.sourceH > 0) ? info.sourceH : info.spriteH;

        t.info.spriteW = recolored.width();
        t.info.spriteH = recolored.height();

        if (req.resizeScale > 0.0f) {
            t.info.sourceW = std::max(1,
                static_cast<int>(std::lround(origSourceW * req.resizeScale)));
            t.info.sourceH = std::max(1,
                static_cast<int>(std::lround(origSourceH * req.resizeScale)));
        } else {
            t.info.sourceW = origSourceW;
            t.info.sourceH = origSourceH;
        }
        if (t.info.sourceW < t.info.spriteW) t.info.sourceW = t.info.spriteW;
        if (t.info.sourceH < t.info.spriteH) t.info.sourceH = t.info.spriteH;

        bool preserveOffset =
            req.preserveOffsetForTableSide &&
            t.name.find("GJ_table_side_001") != std::string::npos;
        if (!preserveOffset && req.resizeScale > 0.0f) {
            t.info.offsetX *= req.resizeScale;
            t.info.offsetY *= req.resizeScale;
        }
        t.info.rotated = false;

        t.pixels = std::move(recolored);
        tinted.push_back(std::move(t));
    }

    if (tinted.empty()) {
        return Err("SheetTinter: no usable frames produced for '{}'", req.outputBaseName);
    }

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

    ImageBuffer outAtlas(pack.sheetWidth, pack.sheetHeight);

    std::unordered_map<std::string, Tinted const*> byName;
    byName.reserve(tinted.size());
    for (auto const& t : tinted) byName.emplace(t.name, &t);

    ParsedSpritesheet outSheet;
    outSheet.frames.reserve(pack.placements.size());

    for (auto const& p : pack.placements) {
        auto it = byName.find(p.id);
        if (it == byName.end()) continue;
        auto const* t = it->second;
        outAtlas.blitOverwrite(p.x, p.y, t->pixels);

        SpriteFrameInfo info = t->info;
        info.name    = p.id;
        info.rectX   = p.x;
        info.rectY   = p.y;
        info.rectW   = p.w;
        info.rectH   = p.h;
        info.rotated = false;
        outSheet.frames.push_back(std::move(info));
    }

    outSheet.metadata = parsed.metadata;
    outSheet.metadata.format              = 3;
    outSheet.metadata.sizeW               = pack.sheetWidth;
    outSheet.metadata.sizeH               = pack.sheetHeight;
    outSheet.metadata.textureFileName     = outputTextureName(req);
    outSheet.metadata.realTextureFileName = outSheet.metadata.textureFileName;
    outSheet.metadata.premultiplyAlpha    = false;

    return buildOutput(outAtlas, outSheet, req,
                       static_cast<int>(tinted.size()), tintedCount, needsReviewCount);
}

}

geode::Result<SheetTinterOutput> SheetTinter::process(SheetTinterRequest const& req) {
    GEODE_UNWRAP_INTO(auto parsed, PlistParser::parseFile(req.sourcePlist));
    if (parsed.frames.empty()) {
        return Err("SheetTinter: sheet '{}' contains no frames", req.outputBaseName);
    }
    GEODE_UNWRAP_INTO(auto atlas, ImageBuffer::loadFromFile(req.sourcePng));

    if (req.resizeScale == 1.0f) {
        return processInPlace(req, parsed, atlas);
    }
    return processRepack(req, parsed, atlas);
}

}
