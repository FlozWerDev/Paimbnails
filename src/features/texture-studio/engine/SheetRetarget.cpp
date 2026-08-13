#include "SheetRetarget.hpp"

#include "../data/ImageBuffer.hpp"
#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"

#include <string_view>
#include <system_error>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// geode.loader unpacks to geode/resources; every other mod to its runtime dir.
std::filesystem::path installedResourceDir(std::string const& modId) {
    if (modId == "geode.loader") {
        return dirs::getGeodeResourcesDir() / "geode.loader";
    }
    if (auto* mod = Loader::get()->getInstalledMod(modId)) {
        return mod->getResourcesDir();
    }
    return {};
}

bool fileExists(std::filesystem::path const& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec) && !ec;
}

std::unordered_map<std::string_view, SpriteFrameInfo const*> indexByName(
    ParsedSpritesheet const& sheet) {

    std::unordered_map<std::string_view, SpriteFrameInfo const*> byName;
    byName.reserve(sheet.frames.size());
    for (auto const& f : sheet.frames) byName.emplace(f.name, &f);
    return byName;
}

}

std::optional<InstalledSheet> SheetRetarget::locate(std::string const& pngRel) {
    auto slash = pngRel.find('/');
    if (slash == std::string::npos) return std::nullopt;

    auto dir = installedResourceDir(pngRel.substr(0, slash));
    if (dir.empty()) return std::nullopt;

    std::string file = pngRel.substr(slash + 1);
    if (file.size() <= 4) return std::nullopt;

    InstalledSheet sheet;
    sheet.pngPath   = dir / file;
    sheet.plistPath = dir / (file.substr(0, file.size() - 4) + ".plist");
    if (!fileExists(sheet.pngPath) || !fileExists(sheet.plistPath)) return std::nullopt;
    return sheet;
}

bool SheetRetarget::sameLayout(ParsedSpritesheet const& a, ParsedSpritesheet const& b) {
    if (a.frames.size() != b.frames.size()) return false;

    auto byName = indexByName(b);
    for (auto const& f : a.frames) {
        auto it = byName.find(f.name);
        if (it == byName.end()) return false;
        auto const& g = *it->second;
        if (f.rectX != g.rectX || f.rectY != g.rectY ||
            f.rectW != g.rectW || f.rectH != g.rectH ||
            f.rotated != g.rotated) {
            return false;
        }
    }
    return true;
}

RetargetOutcome SheetRetarget::conform(std::vector<std::uint8_t> const& processedPng,
                                       std::filesystem::path const& sourcePlist,
                                       std::string const& pngRel) {
    RetargetOutcome outcome;

    auto installed = locate(pngRel);
    if (!installed) {
        outcome.status  = RetargetOutcome::Status::NotInstalled;
        outcome.message = pngRel + ": not installed, atlas layout unverified";
        return outcome;
    }

    auto srcRes = PlistParser::parseFile(sourcePlist);
    if (!srcRes) {
        outcome.status  = RetargetOutcome::Status::Failed;
        outcome.message = pngRel + ": source plist unreadable: " + srcRes.unwrapErr();
        return outcome;
    }
    auto dstRes = PlistParser::parseFile(installed->plistPath);
    if (!dstRes) {
        outcome.status  = RetargetOutcome::Status::Failed;
        outcome.message = pngRel + ": installed plist unreadable: " + dstRes.unwrapErr();
        return outcome;
    }
    auto source         = std::move(srcRes).unwrap();
    auto installedSheet = std::move(dstRes).unwrap();

    if (sameLayout(source, installedSheet)) {
        outcome.status = RetargetOutcome::Status::LayoutMatches;
        return outcome;
    }

    auto processedRes = ImageBuffer::loadFromMemory(
        std::span<std::uint8_t const>(processedPng.data(), processedPng.size()));
    if (!processedRes) {
        outcome.status  = RetargetOutcome::Status::Failed;
        outcome.message = pngRel + ": tinted atlas decode failed: " + processedRes.unwrapErr();
        return outcome;
    }
    auto baseRes = ImageBuffer::loadFromFile(installed->pngPath);
    if (!baseRes) {
        outcome.status  = RetargetOutcome::Status::Failed;
        outcome.message = pngRel + ": installed PNG unreadable: " + baseRes.unwrapErr();
        return outcome;
    }

    auto processed = std::move(processedRes).unwrap();
    auto out       = std::move(baseRes).unwrap();

    auto byName = indexByName(source);
    for (auto const& dst : installedSheet.frames) {
        auto it = byName.find(dst.name);
        if (it == byName.end()) {
            // Added by a newer version; the installed pixels stay untinted.
            ++outcome.missingFrames;
            continue;
        }
        auto const& src = *it->second;
        if (src.rectW != dst.rectW || src.rectH != dst.rectH) {
            // Redrawn at another size; stretching it would distort the sprite.
            ++outcome.missingFrames;
            continue;
        }

        auto pixels = SpritesheetReader::extractFrame(processed, src);
        if (pixels.empty()) {
            ++outcome.missingFrames;
            continue;
        }
        // Packing rotation is a property of the slot, not of the sprite, so it
        // follows the installed plist rather than the snapshot's.
        if (dst.rotated) pixels.rotateCW90();
        out.blitOverwrite(dst.rectX, dst.rectY, pixels);
        ++outcome.matchedFrames;
    }

    auto pngRes = out.encodeAsPng();
    if (!pngRes) {
        outcome.status  = RetargetOutcome::Status::Failed;
        outcome.message = pngRel + ": retargeted PNG encode failed: " + pngRes.unwrapErr();
        return outcome;
    }

    outcome.status   = RetargetOutcome::Status::Retargeted;
    outcome.pngBytes = std::move(pngRes).unwrap();
    outcome.message  = fmt::format(
        "{}: snapshot atlas {}x{} != installed {}x{}, retargeted {} frames ({} left vanilla)",
        pngRel, processed.width(), processed.height(), out.width(), out.height(),
        outcome.matchedFrames, outcome.missingFrames);
    return outcome;
}

}
