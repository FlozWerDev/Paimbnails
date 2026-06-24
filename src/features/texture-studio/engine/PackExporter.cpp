#include "PackExporter.hpp"
#include <Geode/utils/string.hpp>

#include "MediumPort.hpp"
#include "PackMetadataBuilder.hpp"
#include "SheetTinter.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Convert std::vector<uint8_t> to ByteSpan for geode::file::Zip::add.
geode::ByteVector toByteVector(std::vector<std::uint8_t> const& v) {
    return geode::ByteVector(v.begin(), v.end());
}

// Write a sheet pair (PNG + plist) to the zip with the conventional names.
geode::Result<> addSheetToZip(file::Zip& zip,
                              std::string const& baseName,
                              std::string const& qualitySuffix,
                              SheetTinterOutput const& out) {
    std::string pngEntry   = baseName + qualitySuffix + ".png";
    std::string plistEntry = baseName + qualitySuffix + ".plist";

    auto r1 = zip.add(pngEntry, toByteVector(out.pngBytes));
    if (!r1) return Err("zip add {} failed: {}", pngEntry, r1.unwrapErr());

    auto r2 = zip.add(plistEntry, out.plistXml);
    if (!r2) return Err("zip add {} failed: {}", plistEntry, r2.unwrapErr());

    return Ok();
}

}  // anonymous namespace

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

    // Step 1: ensure the output directory exists.
    std::error_code ec;
    auto parentDir = outputZipPath.parent_path();
    if (!parentDir.empty()) {
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            result.errorMessage = std::string("cannot create output dir: ") + ec.message();
            return Err(result.errorMessage);
        }
    }

    // Step 2: open the zip for writing.
    auto zipRes = file::Zip::create(outputZipPath);
    if (!zipRes) {
        result.errorMessage = std::string("cannot open zip for write: ") + zipRes.unwrapErr();
        return Err(result.errorMessage);
    }
    auto zip = std::move(zipRes).unwrap();

    // Step 3: process each sheet and add to zip.
    int totalSheets = static_cast<int>(cfg.sheets.size());
    for (int i = 0; i < totalSheets; ++i) {
        auto const& sel = cfg.sheets[i];

        if (progress) progress(i, totalSheets, sel.baseName);

        SheetTinterRequest req;
        req.sourcePlist               = sel.sourcePlist;
        req.sourcePng                 = sel.sourcePng;
        req.outputBaseName            = sel.baseName;
        req.outputQualitySuffix       = sel.qualitySuffix;  // typically "-uhd"
        req.colors                    = cfg.colors;
        req.brightness                = cfg.brightness;
        req.alternativeGlowOverlay    = cfg.alternativeGlowOverlay;
        req.onlyTintUiSprites         = cfg.onlyTintUiSprites;
        req.tintScope                 = cfg.tintScope;
        req.maskSoftness              = cfg.maskSoftness;
        req.spriteSkip                = cfg.spriteSkip;
        req.spriteColors              = cfg.spriteColors;
        req.spriteImages              = cfg.spriteImages;
        req.resizeScale               = 1.0f;  // primary quality
        req.preserveOffsetForTableSide = true;

        // Primary tint (e.g. the -uhd output).
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

        auto addRes = addSheetToZip(zip, sel.baseName, sel.qualitySuffix, out);
        if (!addRes) {
            sr.success      = false;
            sr.errorMessage = addRes.unwrapErr();
            result.sheetResults.push_back(std::move(sr));
            continue;
        }
        result.sheetResults.push_back(std::move(sr));

        // Medium port (-hd). Only when source is -uhd and the user opted in.
        if (cfg.includeMediumPort && sel.qualitySuffix == "-uhd") {
            auto hdOutRes = MediumPort::generate(req);
            if (!hdOutRes) {
                log::warn("[texture-studio] medium port for '{}' failed: {}",
                    sel.baseName, hdOutRes.unwrapErr());
                continue;
            }
            auto hdOut = std::move(hdOutRes).unwrap();

            // Record a separate result entry for the -hd port so the UI can
            // report it independently.
            SheetExportResult hdSr;
            hdSr.baseName         = sel.baseName;
            hdSr.qualitySuffix    = "-hd";
            hdSr.success          = true;
            hdSr.frameCount       = hdOut.frameCount;
            hdSr.atlasWidth       = hdOut.atlasWidth;
            hdSr.atlasHeight      = hdOut.atlasHeight;
            hdSr.needsReviewCount = hdOut.needsReviewCnt;

            auto hdAddRes = addSheetToZip(zip, sel.baseName, "-hd", hdOut);
            if (!hdAddRes) {
                hdSr.success      = false;
                hdSr.errorMessage = hdAddRes.unwrapErr();
            }
            result.sheetResults.push_back(std::move(hdSr));
        }
    }

    // Step 4: emit metadata files.
    if (auto r = zip.add("pack.json",
            PackMetadataBuilder::buildPackJson(cfg.packName, cfg.author));
        !r) {
        result.errorMessage = std::string("pack.json: ") + r.unwrapErr();
        return Err(result.errorMessage);
    }

    auto colorsJson = PackMetadataBuilder::buildUiColorsJson(cfg);
    auto modsJson   = PackMetadataBuilder::buildModsLayerJson(cfg);
    auto loadJson   = PackMetadataBuilder::buildLoadingLayerJson(cfg);

    if (auto r = zip.addFolder("ui"); !r) {
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

    // Optional custom loading background.
    if (!cfg.customLoadingBgPng.empty()) {
        if (auto r = zip.add("game_bg_custom.png", toByteVector(cfg.customLoadingBgPng));
            !r) {
            // Non-fatal: log it but don't abort the pack.
            log::warn("[texture-studio] custom bg add failed: {}", r.unwrapErr());
        }
    }

    // Step 5: closing the zip flushes to disk (Zip destructor handles it,
    // but we explicitly destroy here so we know the file is committed
    // before computing its size).
    {
        auto _ = std::move(zip);
    }

    // Step 6: stat the output for the result struct.
    std::error_code sec;
    auto sz = std::filesystem::file_size(outputZipPath, sec);
    if (!sec) {
        result.outputZipSizeBytes = static_cast<std::int64_t>(sz);
    }

    // Determine final success: at least one sheet must have succeeded
    // and the metadata must have been written. Per-sheet failures don't
    // make the whole export fail (we still produce a partial pack).
    bool anySuccess = std::any_of(
        result.sheetResults.begin(), result.sheetResults.end(),
        [](SheetExportResult const& sr) { return sr.success; });
    result.success = anySuccess;
    if (!anySuccess) {
        result.errorMessage = "All sheets failed to process";
        return Err(result.errorMessage);
    }

    if (progress) progress(totalSheets, totalSheets, std::string());

    log::info("[texture-studio] export OK: {} ({} bytes), {}/{} sheets",
        geode::utils::string::pathToString(outputZipPath), result.outputZipSizeBytes,
        std::count_if(result.sheetResults.begin(), result.sheetResults.end(),
                      [](auto const& s) { return s.success; }),
        result.sheetResults.size());

    return Ok(std::move(result));
}

}  // namespace paimon::texture_studio
