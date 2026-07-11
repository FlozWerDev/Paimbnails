#include "AnimatedFusionExporter.hpp"

#include "ClusterClassifier.hpp"
#include "ColorClustering.hpp"
#include "FusionAsset.hpp"
#include "FusionEngine.hpp"
#include "LuminanceTinter.hpp"
#include "MaskBuilder.hpp"
#include "SpritePreviewRenderer.hpp"
#include "UiSpriteCatalog.hpp"
#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../persist/FusionStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../utils/GifEncoder.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

TinterOptions makeTintOptions(PackExportConfig const& cfg) {
    TinterOptions o;
    o.brightness             = cfg.brightness;
    o.alternativeGlowOverlay = cfg.alternativeGlowOverlay;
    o.darkOutlineThreshold   = std::clamp(cfg.outlineProtect, 0, 255);
    o.saturation             = cfg.saturation;
    o.contrast               = cfg.contrast;
    return o;
}

// Tint a single logical frame the same way SheetTinter does (no overlay pack
// path — those sprites are rare for user fusions; clustering is the fallback).
ImageBuffer tintFrame(ImageBuffer const& orig,
                      PackExportConfig const& cfg,
                      std::string const& frameName,
                      std::string const& sheetBaseName) {
    if (orig.empty()) return ImageBuffer();
    if (cfg.spriteSkip.count(frameName)) return orig;

    bool imageOverlay = false;
    ImageBuffer customCanvas;
    auto imgIt = cfg.spriteImages.find(frameName);
    if (imgIt != cfg.spriteImages.end()) {
        auto custom = ImageBuffer::loadFromFile(imgIt->second.path);
        if (custom) {
            imageOverlay = imgIt->second.overlay;
            customCanvas = SpritePreviewRenderer::renderCustomImage(
                custom.unwrap(), orig.width(), orig.height(),
                imgIt->second.transform);
        }
    }

    if (!customCanvas.empty() && !imageOverlay) {
        return customCanvas;
    }

    TintColors colors = cfg.colors;
    auto colIt = cfg.spriteColors.find(frameName);
    bool hasColor = colIt != cfg.spriteColors.end();
    if (hasColor) colors = colIt->second;

    auto kind = UiSpriteCatalog::classify(frameName, sheetBaseName);
    bool shouldTint = hasColor
        || !cfg.onlyTintUiSprites
        || UiSpriteCatalog::shouldTint(kind, cfg.tintScope);

    ImageBuffer result;
    if (shouldTint) {
        ClusteringOptions copts;
        copts.k = std::clamp(cfg.clusterPrecision, 2, 10);
        auto clusters   = ColorClustering::compute(orig, copts);
        auto classified = ClusterClassifier::classify(clusters, orig);
        MaskBuilderOptions mopts;
        mopts.softness   = cfg.maskSoftness;
        mopts.edgeRefine = std::clamp(cfg.edgeCleanup, 0, 4);
        auto masks = MaskBuilder::build(orig, classified, mopts);
        result = LuminanceTinter::apply(orig, masks, colors, makeTintOptions(cfg));
    } else {
        result = orig;
    }

    if (!customCanvas.empty()) {
        SpritePreviewRenderer::compositeOver(result, customCanvas);
    }
    return result;
}

// Locate the frame among the pack's selected sheets.
struct FrameLocate {
    ImageBuffer pixels;
    SpriteFrameInfo info;
    std::string sheetBaseName;
};

geode::Result<FrameLocate> locateFrame(PackExportConfig const& cfg,
                                       std::string const& frameName) {
    for (auto const& sheet : cfg.sheets) {
        auto dataRes = FramePixelCache::get().frameData(
            sheet.sourcePlist, sheet.sourcePng, frameName);
        if (!dataRes) continue;
        auto data = std::move(dataRes).unwrap();
        if (data.pixels.empty()) continue;
        FrameLocate loc;
        loc.pixels = std::move(data.pixels);
        loc.info = data.info;
        loc.sheetBaseName = sheet.baseName;
        return Ok(std::move(loc));
    }
    return Err("frame '{}' not found in any selected sheet", frameName);
}

bool isAnimatedTexture(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return false;
    auto ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext != ".gif") {
        // Still try: a .png path might be wrong; only GIFs are multi-frame here.
        return false;
    }
    auto asset = FusionAssetLoader::loadFromFile(path);
    return asset && asset.unwrap()->animated && asset.unwrap()->frameCount() > 1;
}

}  // namespace

std::string fusionGifEntryName(std::string const& frameName) {
    // Frame names almost always end in ".png"; strip and re-append ".gif".
    std::string base = frameName;
    auto dot = base.rfind('.');
    if (dot != std::string::npos) {
        auto ext = base.substr(dot);
        for (char& c : ext) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (ext == ".png" || ext == ".gif" || ext == ".jpg" || ext == ".jpeg"
            || ext == ".webp") {
            base.resize(dot);
        }
    }
    return base + ".gif";
}

geode::Result<AnimatedFusionExport> AnimatedFusionExporter::exportOne(
    PackExportConfig const& cfg,
    std::string const& frameName,
    SpriteFusionOverride const& fusion) {

    if (cfg.spriteSkip.count(frameName)) {
        return Err("sprite '{}' is skipped", frameName);
    }

    auto maskRes = FusionStore::load(fusion.maskPath);
    if (!maskRes) {
        return Err("mask: {}", maskRes.unwrapErr());
    }
    auto payload = std::move(maskRes).unwrap();
    if (payload.mask.empty()) {
        return Err("empty fusion mask");
    }

    auto assetRes = FusionAssetLoader::loadFromFile(fusion.texturePath);
    if (!assetRes) {
        return Err("texture: {}", assetRes.unwrapErr());
    }
    auto asset = assetRes.unwrap();
    if (!asset || asset->empty()) {
        return Err("empty fusion texture");
    }
    if (!asset->animated || asset->frameCount() < 2) {
        return Err("not multi-frame");
    }

    auto located = locateFrame(cfg, frameName);
    if (!located) {
        return Err(located.unwrapErr());
    }
    auto loc = std::move(located).unwrap();

    if (payload.mask.width != loc.pixels.width()
        || payload.mask.height != loc.pixels.height()) {
        return Err("mask size {}x{} != frame {}x{}",
            payload.mask.width, payload.mask.height,
            loc.pixels.width(), loc.pixels.height());
    }

    // Base (tinted, no fusion) once — then stamp each fusion texture frame.
    ImageBuffer baseTinted = tintFrame(loc.pixels, cfg, frameName, loc.sheetBaseName);
    if (baseTinted.empty()) {
        return Err("tint produced empty image");
    }

    FusionApplyOptions opts;
    opts.blendMode = fusion.blendMode;
    opts.opacity   = fusion.opacity > 0.f ? fusion.opacity : payload.opacity;
    opts.transform = fusion.transform.isDefault()
        ? payload.transform : fusion.transform;

    std::vector<paimon::gif::EncodeFrame> gifFrames;
    gifFrames.reserve(asset->frameCount());

    for (std::size_t i = 0; i < asset->frameCount(); ++i) {
        ImageBuffer composed = baseTinted;
        FusionEngine::apply(composed, payload.mask, asset->frameAt(i), opts);

        // Export the logical source-frame canvas (matches in-game layout
        // for offset/sourceW/sourceH sprites) so individual overrides look
        // identical to the sheet-baked first frame.
        ImageBuffer logical = SpritesheetReader::composeLogicalFrame(
            composed, loc.info);

        paimon::gif::EncodeFrame ef;
        ef.width   = logical.width();
        ef.height  = logical.height();
        ef.delayMs = asset->delayAt(i);
        ef.rgba.assign(logical.data(), logical.data() + logical.byteSize());
        if (ef.width <= 0 || ef.height <= 0 || ef.rgba.empty()) continue;
        gifFrames.push_back(std::move(ef));
    }

    if (gifFrames.size() < 2) {
        return Err("fewer than 2 usable frames after compose");
    }

    auto bytes = paimon::gif::encode(gifFrames, /*alphaThreshold=*/16);
    if (bytes.empty()) {
        return Err("GIF encode failed");
    }

    AnimatedFusionExport out;
    out.entryName  = fusionGifEntryName(frameName);
    out.gifBytes   = std::move(bytes);
    out.frameCount = static_cast<int>(gifFrames.size());
    out.width      = gifFrames.front().width;
    out.height     = gifFrames.front().height;
    out.spriteName = frameName;
    return Ok(std::move(out));
}

geode::Result<std::vector<AnimatedFusionExport>> AnimatedFusionExporter::exportAll(
    PackExportConfig const& cfg) {
    std::vector<AnimatedFusionExport> out;
    if (!cfg.exportAnimatedFusions || cfg.spriteFusions.empty()) {
        return Ok(std::move(out));
    }

    out.reserve(cfg.spriteFusions.size());
    for (auto const& [frameName, fusion] : cfg.spriteFusions) {
        if (!isAnimatedTexture(fusion.texturePath)) continue;

        auto one = exportOne(cfg, frameName, fusion);
        if (!one) {
            log::warn("[texture-studio] animated fusion '{}': {}",
                frameName, one.unwrapErr());
            continue;
        }
        log::info("[texture-studio] animated fusion export: {} ({} frames, {}x{})",
            one.unwrap().entryName,
            one.unwrap().frameCount,
            one.unwrap().width,
            one.unwrap().height);
        out.push_back(std::move(one).unwrap());
    }
    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
