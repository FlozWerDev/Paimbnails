#include "PackExporter.hpp"
#include <Geode/utils/string.hpp>

#include "AnimatedFusionExporter.hpp"
#include "MediumPort.hpp"
#include "OverlayTinter.hpp"
#include "PackMetadataBuilder.hpp"
#include "SheetTinter.hpp"
#include "../services/PackGenAssets.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <set>
#include <string_view>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Gate lists ported verbatim from PackGen's script.js.
constexpr std::string_view kDibBaseFiles[] = {
    "hiimjustin000.demons_in_between/DIB_IconSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_IconSheet-uhd.plist",
    "hiimjustin000.demons_in_between/DIB_LongSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_LongSheet-uhd.plist",
    "hiimjustin000.demons_in_between/DIB_ShortSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_ShortSheet-uhd.plist",
    "omgrod.garage_plus/GaragePlus_demonIcon-uhd.png",
};

constexpr std::string_view kMythicFiles[] = {
    "adyagd.godlikefaces/GodlikeDiffSheet-uhd.png",
    "adyagd.godlikefaces/GodlikeDiffSheet-uhd.plist",
    "hiimjustin000.demons_in_between/DIB_LegendaryLongSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_LegendaryLongSheet-uhd.plist",
    "hiimjustin000.demons_in_between/DIB_LegendaryShortSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_LegendaryShortSheet-uhd.plist",
    "hiimjustin000.demons_in_between/DIB_MythicLongSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_MythicLongSheet-uhd.plist",
    "hiimjustin000.demons_in_between/DIB_MythicShortSheet-uhd.png",
    "hiimjustin000.demons_in_between/DIB_MythicShortSheet-uhd.plist",
};

constexpr std::string_view kGoldTitleTargets[] = {
    "GJ_GameSheet03-uhd.png",
    "GJ_GameSheet04-uhd.png",
    "TreasureRoomSheet-uhd.png",
    "raydeeux.deathscreentweaks/newBestFont-uhd.png",
    "raydeeux.deathscreentweaks/newBestFont-hd.png",
    "raydeeux.deathscreentweaks/newBestFont.png",
};

template <std::size_t N>
bool inList(std::string_view const (&list)[N], std::string const& value) {
    for (auto const& item : list) {
        if (item == value) return true;
    }
    return false;
}

bool endsWith(std::string const& s, std::string_view suffix) {
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(std::string const& s, std::string_view prefix) {
    return s.rfind(prefix, 0) == 0;
}

geode::ByteVector toByteVector(std::vector<std::uint8_t> const& v) {
    return geode::ByteVector(v.begin(), v.end());
}

// file::Zip needs every parent folder registered before a nested entry.
geode::Result<> ensureZipFolders(file::Zip& zip, std::string const& entryPath,
                                 std::set<std::string>& created) {
    std::size_t pos = 0;
    while ((pos = entryPath.find('/', pos)) != std::string::npos) {
        std::string dir = entryPath.substr(0, pos);
        if (created.insert(dir).second) {
            if (auto r = zip.addFolder(dir); !r) {
                return Err("zip folder {} failed: {}", dir, r.unwrapErr());
            }
        }
        ++pos;
    }
    return Ok();
}

// Ship the recolored PNG together with its matching plist. Used for the
// user-selected vanilla sheets (GameSheet03/04) — their frame set is stable
// per GD version, so the self-consistent pair is safe — and for every -hd
// medium port, whose re-packed layout requires its own plist. Mod / Geode
// auto-sheets must NOT go through this: use addSheetPngToZip for those.
geode::Result<> addSheetToZip(file::Zip& zip,
                              std::set<std::string>& createdFolders,
                              std::string const& baseName,
                              std::string const& qualitySuffix,
                              SheetTinterOutput const& out) {
    std::string pngEntry   = baseName + qualitySuffix + ".png";
    std::string plistEntry = baseName + qualitySuffix + ".plist";

    GEODE_UNWRAP(ensureZipFolders(zip, pngEntry, createdFolders));

    auto r1 = zip.add(pngEntry, toByteVector(out.pngBytes));
    if (!r1) return Err("zip add {} failed: {}", pngEntry, r1.unwrapErr());

    auto r2 = zip.add(plistEntry, out.plistXml);
    if (!r2) return Err("zip add {} failed: {}", plistEntry, r2.unwrapErr());

    return Ok();
}

// PNG-only variant for auto sheets (mod / geode.loader / extra vanilla
// sheets). Mirrors PackGen's web generator, which never ships plists for
// these: the installed mod's own plist stays authoritative, so a mod that
// updated and ADDED frames since the asset pack was snapshotted keeps
// rendering them (our snapshot plist would make those sprites vanish).
// The in-place tint never changes the atlas layout, so the recolored PNG
// stays valid under the installed plist exactly like PackGen's output.
geode::Result<> addSheetPngToZip(file::Zip& zip,
                                 std::set<std::string>& createdFolders,
                                 std::string const& baseName,
                                 std::string const& qualitySuffix,
                                 SheetTinterOutput const& out) {
    std::string pngEntry = baseName + qualitySuffix + ".png";
    GEODE_UNWRAP(ensureZipFolders(zip, pngEntry, createdFolders));

    auto r = zip.add(pngEntry, toByteVector(out.pngBytes));
    if (!r) return Err("zip add {} failed: {}", pngEntry, r.unwrapErr());
    return Ok();
}

// Downloads an overlay variant if the server has it; empty buffer otherwise.
ImageBuffer loadOptionalOverlay(std::string const& relativePath,
                                std::vector<std::string>& logMessages) {
    auto res = PackGenAssets::get().ensureOptionalFile(relativePath);
    if (!res) {
        logMessages.push_back(relativePath + ": " + res.unwrapErr());
        return ImageBuffer();
    }
    auto maybePath = res.unwrap();
    if (!maybePath.has_value()) return ImageBuffer();

    auto img = ImageBuffer::loadFromFile(*maybePath);
    if (!img) {
        logMessages.push_back(relativePath + ": decode failed: " + img.unwrapErr());
        return ImageBuffer();
    }
    return std::move(img).unwrap();
}

// Gathers every overlay layer that applies to `pngRel` under the current
// options. Gold/demon layers are only probed when their toggle warrants it.
std::shared_ptr<SheetOverlaySources const> buildOverlaySources(
    std::string const& pngRel,
    PackExportConfig const& cfg,
    std::vector<std::string>& logMessages) {

    auto src = std::make_shared<SheetOverlaySources>();
    src->overlay1 = loadOptionalOverlay(packgen_suffix::overlay1(pngRel), logMessages);
    src->overlay2 = loadOptionalOverlay(packgen_suffix::overlay2(pngRel), logMessages);
    src->glow     = loadOptionalOverlay(packgen_suffix::glow(pngRel), logMessages);

    if (cfg.colorGoldTitles && inList(kGoldTitleTargets, pngRel)) {
        src->gold = loadOptionalOverlay(packgen_suffix::gold(pngRel), logMessages);
    }
    if (cfg.colorDemonFaces && pngRel == "GJ_GameSheet03-uhd.png") {
        src->demon1 = loadOptionalOverlay(packgen_suffix::demonFaces1(pngRel), logMessages);
        src->demon2 = loadOptionalOverlay(packgen_suffix::demonFaces2(pngRel), logMessages);
    }
    return src;
}

// PackGen inclusion rules for asset-pack files the user didn't hand-pick.
// geode.loader/* is included on purpose (GE_button_*, GE_square*, tab
// gradient... are the buttons every mod popup uses, and PackGen tints them).
// This is safe because sheets ship WITHOUT a plist (see the auto-sheet loop):
// Geode keeps using its own plist, so its resource-version check never sees
// missing frames.
bool standaloneAllowed(std::string const& rel, PackExportConfig const& cfg) {
    if (rel == "pack.png") return false;  // PackGen's own pack icon
    if (startsWith(rel, "goldFont")) return cfg.tintGoldFont;
    if (inList(kDibBaseFiles, rel))  return cfg.colorDemonFaces;
    if (inList(kMythicFiles, rel))   return cfg.mythicCompat;
    if (rel.find('/') != std::string::npos) return cfg.includeModTextures;
    return true;  // vanilla buttons / squares / sliders
}

TinterOptions standaloneTintOptions(PackExportConfig const& cfg) {
    TinterOptions topts;
    topts.brightness = cfg.brightness;
    topts.alternativeGlowOverlay = cfg.alternativeGlowOverlay;
    topts.saturation = cfg.saturation;
    topts.contrast   = cfg.contrast;
    return topts;
}

}  // namespace

geode::Result<PackExportResult> PackExporter::exportPack(
    PackExportConfig const& cfg,
    std::filesystem::path const& outputZipPath,
    ProgressCallback progress) {

    PackExportResult result;
    result.outputZipPath = outputZipPath;
    result.packId        = PackMetadataBuilder::buildPackId(cfg.packName);

    if (cfg.sheets.empty()) {
        result.errorMessage = "No sheets selected for export";
        return Err(result.errorMessage);
    }

    std::error_code ec;
    auto parentDir = outputZipPath.parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            result.errorMessage = std::string("cannot create output dir: ") + ec.message();
            return Err(result.errorMessage);
        }
    }

    auto zipRes = file::Zip::create(outputZipPath);
    if (!zipRes) {
        result.errorMessage = std::string("cannot open zip for write: ") + zipRes.unwrapErr();
        return Err(result.errorMessage);
    }
    auto zip = std::move(zipRes).unwrap();
    std::set<std::string> zipFolders;
    std::vector<std::string> logMessages;

    // --- PackGen asset pack (precision mode) ------------------------------
    PackGenManifest manifest;
    bool precision = false;
    if (cfg.usePackGenAssets) {
        if (auto r = PackGenAssets::get().ensureManifest(); r) {
            manifest  = PackGenAssets::get().manifest();
            precision = !manifest.empty();
        } else {
            result.precisionNote =
                std::string("PackGen assets unavailable, used auto-detection: ")
                + r.unwrapErr();
            logMessages.push_back(result.precisionNote);
            log::warn("[texture-studio] {}", result.precisionNote);
        }
    }
    result.precisionUsed = precision;

    // Extra sheets shipped by the asset pack (launch screen, treasure room,
    // mod UIs...). GameSheet03/04 stay under the user's explicit selection.
    struct AutoSheet {
        std::string baseName;   // may contain a "modid/" prefix
        std::string pngRel;
        std::string plistRel;
    };
    std::vector<AutoSheet> autoSheets;
    std::vector<std::string> standalonePngs;
    std::vector<std::string> fontFiles;

    if (precision) {
        std::set<std::string> selectedSheetPngs;
        for (auto const& sel : cfg.sheets) {
            selectedSheetPngs.insert(sel.baseName + sel.qualitySuffix + ".png");
        }

        std::set<std::string> sheetPngs;
        for (auto const& rel : manifest.files) {
            if (!endsWith(rel, ".plist")) continue;
            std::string pngRel = rel.substr(0, rel.size() - 6) + ".png";
            if (!manifest.contains(pngRel)) continue;
            sheetPngs.insert(pngRel);

            if (selectedSheetPngs.count(pngRel)) continue;
            if (pngRel == "GJ_GameSheet03-uhd.png" ||
                pngRel == "GJ_GameSheet04-uhd.png") {
                continue;
            }
            if (!standaloneAllowed(pngRel, cfg)) continue;

            AutoSheet s;
            s.pngRel   = pngRel;
            s.plistRel = rel;
            s.baseName = endsWith(pngRel, "-uhd.png")
                ? pngRel.substr(0, pngRel.size() - 8)
                : pngRel.substr(0, pngRel.size() - 4);
            autoSheets.push_back(std::move(s));
        }

        for (auto const& rel : manifest.files) {
            if (endsWith(rel, ".fnt")) {
                fontFiles.push_back(rel);
                continue;
            }
            if (!endsWith(rel, ".png")) continue;
            if (sheetPngs.count(rel)) continue;
            if (selectedSheetPngs.count(rel)) continue;
            if (!standaloneAllowed(rel, cfg)) continue;
            standalonePngs.push_back(rel);
        }
    }

    int totalSheets = static_cast<int>(cfg.sheets.size());
    int totalWork = totalSheets
                  + static_cast<int>(autoSheets.size())
                  + static_cast<int>(standalonePngs.size());
    int workIndex = 0;

    // --- user-selected sheets ---------------------------------------------
    for (int i = 0; i < totalSheets; ++i) {
        auto const& sel = cfg.sheets[i];

        if (progress) progress(workIndex++, totalWork, sel.baseName);

        SheetTinterRequest req;
        req.sourcePlist               = sel.sourcePlist;
        req.sourcePng                 = sel.sourcePng;
        req.outputBaseName            = sel.baseName;
        req.outputQualitySuffix       = sel.qualitySuffix;
        req.colors                    = cfg.colors;
        req.brightness                = cfg.brightness;
        req.alternativeGlowOverlay    = cfg.alternativeGlowOverlay;
        req.onlyTintUiSprites         = cfg.onlyTintUiSprites;
        req.tintScope                 = cfg.tintScope;
        req.maskSoftness              = cfg.maskSoftness;
        req.clusterPrecision          = cfg.clusterPrecision;
        req.edgeCleanup               = cfg.edgeCleanup;
        req.outlineProtect            = cfg.outlineProtect;
        req.saturation                = cfg.saturation;
        req.contrast                  = cfg.contrast;
        req.spriteSkip                = cfg.spriteSkip;
        req.spriteColors              = cfg.spriteColors;
        req.spriteImages              = cfg.spriteImages;
        req.spriteFusions             = cfg.spriteFusions;
        req.resizeScale               = 1.0f;
        req.preserveOffsetForTableSide = true;

        // Precision: swap sources to the asset pack's copy of the sheet so
        // the hand-drawn overlays align with the atlas layout.
        if (precision) {
            std::string pngRel   = sel.baseName + sel.qualitySuffix + ".png";
            std::string plistRel = sel.baseName + sel.qualitySuffix + ".plist";
            if (manifest.contains(pngRel) && manifest.contains(plistRel)) {
                auto pngPath   = PackGenAssets::get().ensureFile(pngRel);
                auto plistPath = PackGenAssets::get().ensureFile(plistRel);
                if (pngPath && plistPath) {
                    req.sourcePng      = pngPath.unwrap();
                    req.sourcePlist    = plistPath.unwrap();
                    req.overlaySources = buildOverlaySources(pngRel, cfg, logMessages);
                } else {
                    logMessages.push_back(pngRel + ": asset fetch failed, using local sheet");
                }
            }
        }

        SheetExportResult sr;
        sr.baseName      = sel.baseName;
        sr.qualitySuffix = sel.qualitySuffix;

        auto outRes = SheetTinter::process(req);
        if (!outRes) {
            sr.success      = false;
            sr.errorMessage = outRes.unwrapErr();
            result.sheetResults.push_back(std::move(sr));
            log::warn("[texture-studio] sheet '{}' failed: {}",
                sel.baseName, outRes.unwrapErr());
            continue;
        }
        auto out = std::move(outRes).unwrap();
        sr.success            = true;
        sr.frameCount         = out.frameCount;
        sr.atlasWidth         = out.atlasWidth;
        sr.atlasHeight        = out.atlasHeight;
        sr.needsReviewCount   = out.needsReviewCnt;

        auto addRes = addSheetToZip(zip, zipFolders, sel.baseName, sel.qualitySuffix, out);
        if (!addRes) {
            sr.success      = false;
            sr.errorMessage = addRes.unwrapErr();
            result.sheetResults.push_back(std::move(sr));
            continue;
        }
        result.sheetResults.push_back(std::move(sr));

        if (cfg.includeMediumPort && sel.qualitySuffix == "-uhd") {
            auto hdOutRes = MediumPort::generate(req);
            if (!hdOutRes) {
                log::warn("[texture-studio] medium port for '{}' failed: {}",
                    sel.baseName, hdOutRes.unwrapErr());
                continue;
            }
            auto hdOut = std::move(hdOutRes).unwrap();

            SheetExportResult hdSr;
            hdSr.baseName         = sel.baseName;
            hdSr.qualitySuffix    = "-hd";
            hdSr.success          = true;
            hdSr.frameCount       = hdOut.frameCount;
            hdSr.atlasWidth       = hdOut.atlasWidth;
            hdSr.atlasHeight      = hdOut.atlasHeight;
            hdSr.needsReviewCount = hdOut.needsReviewCnt;

            auto hdAddRes = addSheetToZip(zip, zipFolders, sel.baseName, "-hd", hdOut);
            if (!hdAddRes) {
                hdSr.success      = false;
                hdSr.errorMessage = hdAddRes.unwrapErr();
            }
            result.sheetResults.push_back(std::move(hdSr));
        }
    }

    // --- asset-pack sheets (launch screen, treasure room, mod UIs) ---------
    for (auto const& autoSheet : autoSheets) {
        if (progress) progress(workIndex++, totalWork, autoSheet.baseName);

        auto pngPath   = PackGenAssets::get().ensureFile(autoSheet.pngRel);
        auto plistPath = PackGenAssets::get().ensureFile(autoSheet.plistRel);
        if (!pngPath || !plistPath) {
            ++result.standaloneFailed;
            logMessages.push_back(autoSheet.pngRel + ": download failed");
            continue;
        }

        SheetTinterRequest req;
        req.sourcePng               = pngPath.unwrap();
        req.sourcePlist             = plistPath.unwrap();
        req.outputBaseName          = autoSheet.baseName;
        req.outputQualitySuffix     = "-uhd";
        req.colors                  = cfg.colors;
        req.brightness              = cfg.brightness;
        req.alternativeGlowOverlay  = cfg.alternativeGlowOverlay;
        // Overlay ink decides coverage on asset-pack sheets; the name-based
        // classifier doesn't know mod sprites and would block everything.
        req.onlyTintUiSprites       = false;
        req.saturation              = cfg.saturation;
        req.contrast                = cfg.contrast;
        req.spriteSkip              = cfg.spriteSkip;
        req.spriteImages            = cfg.spriteImages;
        req.spriteFusions           = cfg.spriteFusions;
        req.overlaySources          = buildOverlaySources(autoSheet.pngRel, cfg, logMessages);

        if (!req.overlaySources->any()) {
            // No hand-drawn masks for this sheet: nothing to recolor, and a
            // vanilla copy would only bloat the zip.
            if (!inList(kDibBaseFiles, autoSheet.pngRel) &&
                !inList(kMythicFiles, autoSheet.pngRel)) {
                continue;
            }
            // DIB/Godlike sheets are wanted even untinted (they replace the
            // demon faces the base game lacks).
        }

        auto outRes = SheetTinter::process(req);
        if (!outRes) {
            ++result.standaloneFailed;
            logMessages.push_back(autoSheet.pngRel + ": " + outRes.unwrapErr());
            continue;
        }
        auto out = std::move(outRes).unwrap();

        // PNG only — the installed mod / Geode plist must stay authoritative
        // (see addSheetPngToZip).
        if (auto r = addSheetPngToZip(zip, zipFolders, autoSheet.baseName, "-uhd", out); !r) {
            ++result.standaloneFailed;
            logMessages.push_back(autoSheet.pngRel + ": " + r.unwrapErr());
            continue;
        }
        ++result.standaloneProcessed;

        if (cfg.includeMediumPort) {
            // The -hd port re-packs the atlas, so it does need its own plist
            // (PackGen ships this same pair in medium-port mode).
            if (auto hdOutRes = MediumPort::generate(req)) {
                auto hdOut = std::move(hdOutRes).unwrap();
                if (auto r = addSheetToZip(zip, zipFolders, autoSheet.baseName, "-hd", hdOut); !r) {
                    logMessages.push_back(autoSheet.pngRel + " (hd): " + r.unwrapErr());
                }
            } else {
                logMessages.push_back(autoSheet.pngRel + " (hd): " + hdOutRes.unwrapErr());
            }
        }
    }

    // --- standalone textures (buttons, squares, sliders, mod icons) --------
    for (auto const& rel : standalonePngs) {
        if (progress) progress(workIndex++, totalWork, rel);

        auto basePath = PackGenAssets::get().ensureFile(rel);
        if (!basePath) {
            ++result.standaloneFailed;
            logMessages.push_back(rel + ": download failed: " + basePath.unwrapErr());
            continue;
        }
        auto baseImg = ImageBuffer::loadFromFile(basePath.unwrap());
        if (!baseImg) {
            ++result.standaloneFailed;
            logMessages.push_back(rel + ": decode failed: " + baseImg.unwrapErr());
            continue;
        }
        auto base = std::move(baseImg).unwrap();

        OverlayImages ov;
        ov.overlay1 = loadOptionalOverlay(packgen_suffix::overlay1(rel), logMessages);
        ov.overlay2 = loadOptionalOverlay(packgen_suffix::overlay2(rel), logMessages);
        ov.glow     = loadOptionalOverlay(packgen_suffix::glow(rel), logMessages);
        if (cfg.colorGoldTitles && inList(kGoldTitleTargets, rel)) {
            ov.gold = loadOptionalOverlay(packgen_suffix::gold(rel), logMessages);
        }

        if (!ov.anyUsable(base.width(), base.height())) {
            // PackGen ships untinted bases too, but for us that's dead
            // weight — the game already has the original texture.
            logMessages.push_back(rel + ": no overlays, skipped");
            continue;
        }

        auto tinted = OverlayTinter::apply(base, ov, cfg.colors,
                                           standaloneTintOptions(cfg));
        auto png = tinted.encodeAsPng();
        if (!png) {
            ++result.standaloneFailed;
            logMessages.push_back(rel + ": encode failed: " + png.unwrapErr());
            continue;
        }

        if (auto r = ensureZipFolders(zip, rel, zipFolders); !r) {
            ++result.standaloneFailed;
            logMessages.push_back(rel + ": " + r.unwrapErr());
            continue;
        }
        if (auto r = zip.add(rel, toByteVector(png.unwrap())); !r) {
            ++result.standaloneFailed;
            logMessages.push_back(rel + ": zip add failed: " + r.unwrapErr());
            continue;
        }
        ++result.standaloneProcessed;

        // Medium port: halve -uhd textures unless the pack says not to
        // (fonts and pre-scaled atlases break when resized).
        if (cfg.includeMediumPort && endsWith(rel, "-uhd.png") &&
            !manifest.isNoScaling(rel)) {
            auto half = tinted.resizedBilinear(
                std::max(1, tinted.width() / 2), std::max(1, tinted.height() / 2));
            if (auto hdPng = half.encodeAsPng()) {
                std::string hdRel = rel.substr(0, rel.size() - 8) + "-hd.png";
                if (auto r = zip.add(hdRel, toByteVector(hdPng.unwrap())); !r) {
                    logMessages.push_back(hdRel + ": zip add failed: " + r.unwrapErr());
                }
            }
        }
    }

    // --- bitmap font descriptors -------------------------------------------
    for (auto const& rel : fontFiles) {
        bool wanted = startsWith(rel, "goldFont")
            ? cfg.tintGoldFont
            : (cfg.includeModTextures && rel.find('/') != std::string::npos);
        if (!wanted) continue;

        auto path = PackGenAssets::get().ensureFile(rel);
        if (!path) {
            logMessages.push_back(rel + ": download failed");
            continue;
        }
        auto bytes = file::readBinary(path.unwrap());
        if (!bytes) {
            logMessages.push_back(rel + ": read failed");
            continue;
        }
        if (auto r = ensureZipFolders(zip, rel, zipFolders); !r) {
            logMessages.push_back(rel + ": " + r.unwrapErr());
            continue;
        }
        if (auto r = zip.add(rel, bytes.unwrap()); !r) {
            logMessages.push_back(rel + ": zip add failed: " + r.unwrapErr());
        }
    }

    // --- animated fusion GIFs (ImagePlus / Happy Textures) ----------------
    // Sheets already contain frame 0 of each fusion. Multi-frame textures also
    // ship as standalone .gif files so ImagePlus can animate them in-game.
    if (cfg.exportAnimatedFusions && !cfg.spriteFusions.empty()) {
        if (progress) {
            progress(workIndex, totalWork + 1, "Animated fusions...");
        }
        auto animRes = AnimatedFusionExporter::exportAll(cfg);
        if (animRes) {
            for (auto& anim : animRes.unwrap()) {
                if (anim.gifBytes.empty() || anim.entryName.empty()) continue;
                if (auto r = ensureZipFolders(zip, anim.entryName, zipFolders); !r) {
                    logMessages.push_back(anim.entryName + ": " + r.unwrapErr());
                    continue;
                }
                if (auto r = zip.add(anim.entryName, toByteVector(anim.gifBytes)); !r) {
                    logMessages.push_back(anim.entryName + ": zip add failed: "
                        + r.unwrapErr());
                    continue;
                }
                ++result.animatedFusionCount;
                logMessages.push_back(fmt::format(
                    "animated: {} ({} frames)", anim.entryName, anim.frameCount));
            }
        } else {
            logMessages.push_back(
                std::string("animated fusions: ") + animRes.unwrapErr());
        }
    }

    // --- metadata -----------------------------------------------------------
    if (auto r = zip.add("pack.json",
            PackMetadataBuilder::buildPackJson(cfg.packName, cfg.author));
        !r) {
        result.errorMessage = std::string("pack.json: ") + r.unwrapErr();
        return Err(result.errorMessage);
    }

    // Optional readme so pack users know why .gif files are present.
    if (result.animatedFusionCount > 0) {
        std::string readme =
            "Animated fusion sprites\n"
            "=======================\n\n"
            "This pack includes multi-frame .gif sprites from Texture Studio "
            "Fusion mode.\n\n"
            "Requirements for animation in-game:\n"
            "  - geode.texture-loader (or Happy Textures)\n"
            "  - prevter.imageplus  (GIF / animated formats)\n\n"
            "Without ImagePlus the static PNG frames inside the spritesheets "
            "still work (first frame of each fusion).\n";
        if (auto r = zip.add("ANIMATED_FUSIONS.txt", readme); !r) {
            logMessages.push_back(
                std::string("ANIMATED_FUSIONS.txt: ") + r.unwrapErr());
        }
    }

    auto colorsJson = PackMetadataBuilder::buildUiColorsJson(cfg);
    auto modsJson   = PackMetadataBuilder::buildModsLayerJson(cfg);
    auto loadJson   = PackMetadataBuilder::buildLoadingLayerJson(cfg);

    if (auto r = ensureZipFolders(zip, "ui/x", zipFolders); !r) {
        result.errorMessage = std::string("ui/ folder: ") + r.unwrapErr();
        return Err(result.errorMessage);
    }
    if (auto r = zip.add("ui/colors.json", colorsJson); !r) {
        result.errorMessage = std::string("ui/colors.json: ") + r.unwrapErr();
        return Err(result.errorMessage);
    }
    if (auto r = zip.add("ui/ModsLayer.json", modsJson); !r) {
        result.errorMessage = std::string("ui/ModsLayer.json: ") + r.unwrapErr();
        return Err(result.errorMessage);
    }
    if (auto r = zip.add("ui/LoadingLayer.json", loadJson); !r) {
        result.errorMessage = std::string("ui/LoadingLayer.json: ") + r.unwrapErr();
        return Err(result.errorMessage);
    }

    // Custom loading background. LoadingLayer.json references
    // "LoadingLayerBG.png"; the -uhd/-hd suffixes are resolved per quality.
    if (!cfg.customLoadingBgPng.empty()) {
        if (auto r = zip.add("LoadingLayerBG-uhd.png",
                             toByteVector(cfg.customLoadingBgPng)); !r) {
            log::warn("[texture-studio] custom bg add failed: {}", r.unwrapErr());
        } else if (cfg.includeMediumPort) {
            if (auto img = ImageBuffer::loadFromMemory(std::span(
                    cfg.customLoadingBgPng.data(), cfg.customLoadingBgPng.size()))) {
                auto full = std::move(img).unwrap();
                auto half = full.resizedBilinear(
                    std::max(1, full.width() / 2), std::max(1, full.height() / 2));
                if (auto hdPng = half.encodeAsPng()) {
                    (void)zip.add("LoadingLayerBG-hd.png", toByteVector(hdPng.unwrap()));
                }
            }
        }
    }

    // --- README + generation log (PackGen tradition) ------------------------
    auto fmtColor = [](cocos2d::ccColor3B c) {
        return fmt::format("RGB({}, {}, {})", c.r, c.g, c.b);
    };
    std::string readme = fmt::format(
        "# {} - Generated Pack\n\n"
        "## Details:\n"
        "- Pack Name: {}\n"
        "- Color 1: {}\n"
        "- Color 2: {}\n"
        "- Glow Color: {}\n"
        "- Brightness: {}\n"
        "- Transparent Lists: {}\n"
        "- Medium (HD) Port: {}\n"
        "- Precision overlays: {}\n\n"
        "Generated with Paimon Texture Studio (Paimbnails).\n"
        "Precision overlay masks by Asterveila (PackGen): https://packgenweb.pages.dev/\n",
        cfg.packName, cfg.packName,
        fmtColor(cfg.colors.color1), fmtColor(cfg.colors.color2),
        fmtColor(cfg.colors.glow), cfg.brightness,
        cfg.transparentLists ? "Yes" : "No",
        cfg.includeMediumPort ? "Yes" : "No",
        precision ? "Yes" : "No (auto-detection)");
    if (auto r = zip.add("README.md", readme); !r) {
        log::warn("[texture-studio] README add failed: {}", r.unwrapErr());
    }

    std::string logText = fmt::format(
        "Paimon Texture Studio generation log\n"
        "=====================================\n\n"
        "Sheets: {}  |  Extra sheets: {}  |  Standalone files ok: {}  |  failed: {}\n"
        "Precision overlays: {}\n\n",
        totalSheets, autoSheets.size(),
        result.standaloneProcessed, result.standaloneFailed,
        precision ? "active" : "inactive");
    if (logMessages.empty()) {
        logText += "All files processed cleanly.\n";
    } else {
        for (auto const& msg : logMessages) {
            logText += "- " + msg + "\n";
        }
    }
    if (auto r = zip.add("generation_log.txt", logText); !r) {
        log::warn("[texture-studio] log add failed: {}", r.unwrapErr());
    }

    // Destroy the zip to flush it to disk before reading its size below.
    {
        auto _ = std::move(zip);
    }

    std::error_code sec;
    auto sz = std::filesystem::file_size(outputZipPath, sec);
    if (!sec) {
        result.outputZipSizeBytes = static_cast<std::int64_t>(sz);
    }

    // Per-sheet failures don't fail the whole export; we still ship a partial pack.
    bool anySuccess = std::any_of(
        result.sheetResults.begin(), result.sheetResults.end(),
        [](SheetExportResult const& sr) { return sr.success; });
    result.success = anySuccess;
    if (!anySuccess) {
        result.errorMessage = "All sheets failed to process";
        return Err(result.errorMessage);
    }

    if (progress) progress(totalWork, totalWork, std::string());

    log::info("[texture-studio] export OK: {} ({} bytes), {}/{} sheets, "
              "{} standalone (+{} failed), precision={}",
        geode::utils::string::pathToString(outputZipPath), result.outputZipSizeBytes,
        std::count_if(result.sheetResults.begin(), result.sheetResults.end(),
                      [](auto const& s) { return s.success; }),
        result.sheetResults.size(),
        result.standaloneProcessed, result.standaloneFailed,
        precision);

    return Ok(std::move(result));
}

}  // namespace paimon::texture_studio
