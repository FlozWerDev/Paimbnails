#include "GdResourcesLocator.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <unordered_map>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// Strip a quality suffix from a stem, returning (baseName, suffix).
// "GJ_GameSheet01-uhd" → ("GJ_GameSheet01", "-uhd")
// "GJ_GameSheet01-hd"  → ("GJ_GameSheet01", "-hd")
// "GJ_GameSheet01"     → ("GJ_GameSheet01", "")
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

// Quality ranking: bigger = better. Used to pick the best variant when a
// base name has multiple qualities present.
int qualityRank(std::string_view suffix) {
    if (suffix == "-uhd") return 3;
    if (suffix == "-hd")  return 2;
    if (suffix.empty())   return 1;  // unsuffixed = "low"/"sd" in GD parlance
    return 0;  // unknown suffix, lowest priority
}

// File size in bytes (0 if missing/unreadable).
std::int64_t safeFileSize(std::filesystem::path const& p) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<std::int64_t>(sz);
}

}  // anonymous namespace

std::filesystem::path GdResourcesLocator::resourcesDir() {
    return geode::dirs::getResourcesDir();
}

geode::Result<std::vector<DetectedSheet>> GdResourcesLocator::detectVanillaSheets() {
    auto dir = resourcesDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return Err("GdResourcesLocator: resources dir does not exist: {}", geode::utils::string::pathToString(dir));
    }
    return detectInDirectory(dir);
}

geode::Result<std::vector<DetectedSheet>> GdResourcesLocator::detectInDirectory(
    std::filesystem::path const& dir) {

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return Err("GdResourcesLocator: not a directory: {}", geode::utils::string::pathToString(dir));
    }

    // Pass 1: collect every .plist and every .png keyed by stem.
    std::unordered_map<std::string, std::filesystem::path> plists;  // stem → path
    std::unordered_map<std::string, std::filesystem::path> pngs;
    plists.reserve(64);
    pngs.reserve(64);

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto const& p = entry.path();
        auto ext = geode::utils::string::pathToString(p.extension());
        // case-insensitive ext compare
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto stem = geode::utils::string::pathToString(p.stem());
        if (ext == ".plist") plists.emplace(stem, p);
        else if (ext == ".png") pngs.emplace(stem, p);
    }

    // Pass 2: pair plists with their png by stem, group by base name (without
    // quality suffix), and keep only the best-quality variant per base.
    struct Candidate {
        DetectedSheet sheet;
        int rank = 0;
    };
    std::unordered_map<std::string, Candidate> bestByBase;

    for (auto const& [stem, plistPath] : plists) {
        auto pngIt = pngs.find(stem);
        if (pngIt == pngs.end()) continue;  // plist without matching png

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
        // frameCount is left at -1; callers can sniff it lazily if they
        // need to display a count (parsing every plist eagerly would waste
        // time when there are 50+ sheets in Resources).

        bestByBase[baseName] = Candidate{std::move(ds), rank};
    }

    std::vector<DetectedSheet> out;
    out.reserve(bestByBase.size());
    for (auto& [name, cand] : bestByBase) {
        out.push_back(std::move(cand.sheet));
    }
    // Stable, alphabetical order — feels like a "real" file picker.
    std::sort(out.begin(), out.end(),
        [](DetectedSheet const& a, DetectedSheet const& b) {
            return a.baseName < b.baseName;
        });
    return Ok(std::move(out));
}

}  // namespace paimon::texture_studio
