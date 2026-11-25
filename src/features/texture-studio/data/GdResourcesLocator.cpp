#include "../../../utils/FilesystemPath.hpp"
#include "GdResourcesLocator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/cocos/platform/CCFileUtils.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

std::pair<std::string, std::string> splitQualitySuffix(std::string stem) {
    static constexpr std::string_view kSuffixes[] = {"-uhd", "-hd"};
    for (auto suf : kSuffixes) {
        if (stem.size() > suf.size() &&
            stem.compare(stem.size() - suf.size(), suf.size(), suf) == 0) {
            return {stem.substr(0, stem.size() - suf.size()), std::string(suf)};
        }
    }
    return {std::move(stem), std::string()};
}

int qualityRank(std::string_view suffix) {
    if (suffix == "-uhd") return 3;
    if (suffix == "-hd")  return 2;
    if (suffix.empty())   return 1;
    return 0;
}

std::int64_t safeFileSize(std::filesystem::path const& p) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<std::int64_t>(sz);
}

#if defined(GEODE_IS_MOBILE)
// On Android/iOS vanilla GD sheets live inside the APK/bundle rather than a
// plain directory, so probe CCFileUtils for the standard sheet catalog.
constexpr std::array<char const*, 22> kKnownVanillaSheetBases = {
    "GJ_GameSheet",
    "GJ_GameSheet02",
    "GJ_GameSheet03",
    "GJ_GameSheet04",
    "GJ_GameSheetGlow",
    "GJ_GameSheetIcons",
    "GJ_LaunchSheet",
    "CCControlColourPickerSpriteSheet",
    "DungeonSheet",
    "FireSheet_01",
    "GauntletSheet",
    "PixelSheet_01",
    "SecretSheet",
    "ShopSheet",
    "TowerSheet",
    "TreasureRoomSheet",
    "WorldSheet",
    "GJ_ParticleSheet",
    "PlayerExplosion_01",
    "PlayerExplosion_02",
    "PlayerExplosion_03",
    "PlayerExplosion_04",
};

std::string preferredAndroidSuffix() {
    auto* director = CCDirector::sharedDirector();
    float const scale = director ? director->getContentScaleFactor() : 1.0f;
    if (scale >= 3.5f) return "-uhd";
    if (scale >= 1.5f) return "-hd";
    return {};
}
#endif

}  // anonymous namespace

bool GdResourcesLocator::readBundledAsset(std::string const& relativeName, std::vector<std::uint8_t>& out) {
    out.clear();
    if (relativeName.empty()) return false;
    auto* fu = CCFileUtils::sharedFileUtils();
    if (!fu) return false;
    unsigned long size = 0;
    bool const prevPopup = fu->isPopupNotify();
    fu->setPopupNotify(false);
    unsigned char* raw = fu->getFileData(relativeName.c_str(), "rb", &size);
    fu->setPopupNotify(prevPopup);
    if (!raw || size == 0) {
        delete[] raw;
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    std::memcpy(out.data(), raw, static_cast<std::size_t>(size));
    delete[] raw;
    return true;
}

bool GdResourcesLocator::bundledAssetExists(std::string const& relativeName) {
    if (relativeName.empty()) return false;
    auto* fu = CCFileUtils::sharedFileUtils();
    if (!fu) return false;
    std::string const resolved = fu->fullPathForFilename(relativeName.c_str(), false);
    if (resolved.empty()) return false;
    bool const prevPopup = fu->isPopupNotify();
    fu->setPopupNotify(false);
    bool const exists = fu->isFileExist(resolved);
    fu->setPopupNotify(prevPopup);
    return exists;
}

std::filesystem::path GdResourcesLocator::resourcesDir() {
    return paimon::paths::toStdPath(geode::dirs::getResourcesDir());
}

geode::Result<std::vector<DetectedSheet>> GdResourcesLocator::detectVanillaSheets() {
    auto dir = resourcesDir();
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) {
        return detectInDirectory(dir);
    }
#if defined(GEODE_IS_MOBILE)
    std::string const pref = preferredAndroidSuffix();
    std::vector<std::string> suffixOrder;
    if (!pref.empty()) suffixOrder.push_back(pref);
    for (auto const* cand : {"-uhd", "-hd", ""}) {
        if (pref != cand) suffixOrder.emplace_back(cand);
    }

    std::vector<DetectedSheet> out;
    out.reserve(kKnownVanillaSheetBases.size());
    for (auto const* base : kKnownVanillaSheetBases) {
        for (auto const& suf : suffixOrder) {
            std::string plistRel = std::string(base) + suf + ".plist";
            std::string pngRel   = std::string(base) + suf + ".png";
            if (bundledAssetExists(plistRel) && bundledAssetExists(pngRel)) {
                DetectedSheet ds;
                ds.baseName = base;
                ds.qualitySuffix = suf;
                ds.plistPath = plistRel;
                ds.pngPath = pngRel;
                ds.fileSize = 0;
                out.push_back(std::move(ds));
                break;
            }
        }
    }
    if (!out.empty()) {
        std::sort(out.begin(), out.end(),
            [](DetectedSheet const& a, DetectedSheet const& b) {
                return a.baseName < b.baseName;
            });
        return Ok(std::move(out));
    }
#endif
    return Err("GdResourcesLocator: resources dir does not exist: {}", paimon::paths::toUtf8(dir));
}

geode::Result<std::vector<DetectedSheet>> GdResourcesLocator::detectInDirectory(
    std::filesystem::path const& dir) {

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return Err("GdResourcesLocator: not a directory: {}", paimon::paths::toUtf8(dir));
    }

    std::unordered_map<std::string, std::filesystem::path> plists;
    std::unordered_map<std::string, std::filesystem::path> pngs;
    plists.reserve(64);
    pngs.reserve(64);

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto const& p = entry.path();
        auto ext = paimon::paths::toUtf8(p.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto stem = paimon::paths::toUtf8(p.stem());
        if (ext == ".plist") plists.emplace(stem, p);
        else if (ext == ".png") pngs.emplace(stem, p);
    }

    struct Candidate {
        DetectedSheet sheet;
        int rank = 0;
    };
    std::unordered_map<std::string, Candidate> bestByBase;

    for (auto const& [stem, plistPath] : plists) {
        auto pngIt = pngs.find(stem);
        if (pngIt == pngs.end()) continue;

        auto [baseName, suffix] = splitQualitySuffix(stem);
        int rank = qualityRank(suffix);

        auto it = bestByBase.find(baseName);
        if (it != bestByBase.end() && it->second.rank >= rank) continue;

        DetectedSheet ds;
        ds.baseName       = baseName;
        ds.qualitySuffix  = suffix;
        ds.plistPath      = plistPath;
        ds.pngPath        = pngIt->second;
        ds.fileSize       = safeFileSize(pngIt->second);
        // framecount stays -1: sniffed lazily instead of parsing every plist.

        bestByBase[baseName] = Candidate{std::move(ds), rank};
    }

    std::vector<DetectedSheet> out;
    out.reserve(bestByBase.size());
    for (auto& [name, cand] : bestByBase) {
        out.push_back(std::move(cand.sheet));
    }
    std::sort(out.begin(), out.end(),
        [](DetectedSheet const& a, DetectedSheet const& b) {
            return a.baseName < b.baseName;
        });
    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
