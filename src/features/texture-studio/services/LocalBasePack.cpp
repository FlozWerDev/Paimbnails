#include "LocalBasePack.hpp"

#include "../data/GdResourcesLocator.hpp"
#include "../persist/SlotPaths.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

// reject paths that escape the indexed directories.
bool isSafeRelativePath(std::string const& rel) {
    if (rel.empty() || rel.front() == '/' || rel.find(':') != std::string::npos) {
        return false;
    }
    return rel.find("..") == std::string::npos;
}

std::string replaceExtensionSuffix(std::string const& pngRelPath,
                                   char const* overlayTag) {
    constexpr std::string_view kPng = ".png";
    if (pngRelPath.size() < kPng.size() ||
        pngRelPath.compare(pngRelPath.size() - kPng.size(), kPng.size(), kPng) != 0) {
        return pngRelPath + overlayTag;
    }
    return pngRelPath.substr(0, pngRelPath.size() - kPng.size())
         + overlayTag + std::string(kPng);
}

std::string lowerExt(std::filesystem::path const& p) {
    auto ext = geode::utils::string::pathToString(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

// filenames are safe for one flat snapshot directory.
std::string snapshotFileName(std::string const& pngRel) {
    std::string out = pngRel;
    for (auto& c : out) {
        if (c == '/' || c == '\\' || c == ':') c = '_';
    }
    return out;
}

}  // namespace

namespace packgen_suffix {
std::string overlay1(std::string const& p)    { return replaceExtensionSuffix(p, "_OVERLAY1"); }
std::string overlay2(std::string const& p)    { return replaceExtensionSuffix(p, "_OVERLAY2"); }
std::string glow(std::string const& p)        { return replaceExtensionSuffix(p, "_GLOWOVERLAY"); }
std::string gold(std::string const& p)        { return replaceExtensionSuffix(p, "_GOLDOVERLAY"); }
std::string demonFaces1(std::string const& p) { return replaceExtensionSuffix(p, "_OVERLAY1_DEMONFACES"); }
std::string demonFaces2(std::string const& p) { return replaceExtensionSuffix(p, "_OVERLAY2_DEMONFACES"); }
}  // namespace packgen_suffix

bool LocalBaseManifest::contains(std::string const& relativePath) const {
    return std::find(files.begin(), files.end(), relativePath) != files.end();
}

bool LocalBaseManifest::isNoScaling(std::string const& relativePath) const {
    return std::find(noScalingFiles.begin(), noScalingFiles.end(), relativePath)
        != noScalingFiles.end();
}

LocalBasePack& LocalBasePack::get() {
    static LocalBasePack instance;
    return instance;
}

bool LocalBasePack::isManifestLoaded() const {
    std::lock_guard lock(m_mutex);
    return !m_manifest.empty();
}

LocalBaseManifest LocalBasePack::manifest() const {
    std::lock_guard lock(m_mutex);
    return m_manifest;
}

std::filesystem::path LocalBasePack::snapshotDir() const {
    return SlotPaths::rootDir() / "snapshots";
}

void LocalBasePack::rebuildIndexLocked() {
    m_manifest.files.clear();
    m_manifest.noScalingFiles.clear();
    m_index.clear();

    auto addFile = [&](std::string rel, std::filesystem::path abs) {
        if (!isSafeRelativePath(rel)) return;
        if (m_index.find(rel) != m_index.end()) return;
        m_index.emplace(rel, abs);
        m_manifest.files.push_back(std::move(rel));
    };

    auto addPaired = [&](std::string const& prefix, DetectedSheet const& s) {
        addFile(prefix + s.baseName + s.qualitySuffix + ".png", s.pngPath);
        addFile(prefix + s.baseName + s.qualitySuffix + ".plist", s.plistPath);
    };

    // loose files the pair scanner skips: fonts and atlas-less pngs.
    auto sweepLooseFiles = [&](std::filesystem::path const& dir,
                               std::string const& prefix) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) || ec) return;
        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            auto const& p = entry.path();
            auto ext = lowerExt(p);
            std::string name = geode::utils::string::pathToString(p.filename());
            if (ext == ".fnt") {
                addFile(prefix + name, p);
            } else if (ext == ".png") {
                auto plistSibling = p.parent_path() / p.stem();
                plistSibling += ".plist";
                std::error_code sec;
                if (!std::filesystem::is_regular_file(plistSibling, sec) || sec) {
                    addFile(prefix + name, p);
                }
            }
        }
    };

    // vanilla sheets: best quality per base name, rels are bare filenames.
    if (auto vanilla = GdResourcesLocator::detectVanillaSheets()) {
        for (auto const& s : vanilla.unwrap()) addPaired("", s);
    }
    sweepLooseFiles(GdResourcesLocator::resourcesDir(), "");

    // mod + geode sheets, rels prefixed so sheetretarget::locate resolves them.
    std::string ownId;
    try {
        if (auto* self = Mod::get()) ownId = std::string(self->getID());
    } catch (...) {}
    if (auto* loader = Loader::get()) {
        for (auto* mod : loader->getAllMods()) {
            if (!mod) continue;
            std::string id;
            try {
                id = std::string(mod->getID());
            } catch (...) { continue; }
            if (id.empty() || id == ownId) continue;

            std::filesystem::path dir;
            if (id == "geode.loader") {
                dir = dirs::getGeodeResourcesDir() / "geode.loader";
            } else {
                try {
                    dir = mod->getResourcesDir();
                } catch (...) { continue; }
            }
            if (dir.empty()) continue;

            std::string prefix = id + "/";
            if (auto sheets = GdResourcesLocator::detectInDirectory(dir)) {
                for (auto const& s : sheets.unwrap()) addPaired(prefix, s);
            }
            sweepLooseFiles(dir, prefix);
        }
    }

    std::sort(m_manifest.files.begin(), m_manifest.files.end());
    m_scanned = true;
}

geode::Result<> LocalBasePack::ensureManifest() {
    std::lock_guard lock(m_mutex);
    if (!m_cacheCleaned) {
        m_cacheCleaned = true;
        std::error_code ec;
        std::filesystem::remove_all(SlotPaths::rootDir() / "packgen-cache", ec);
    }
    rebuildIndexLocked();
    if (m_manifest.empty()) {
        return geode::Err("no local base sheets found in the game or mod resources");
    }
    return geode::Ok();
}

geode::Result<std::filesystem::path> LocalBasePack::ensureFile(
    std::string const& relativePath) {

    if (!isSafeRelativePath(relativePath)) {
        return geode::Err("unsafe path: {}", relativePath);
    }
    {
        std::lock_guard lock(m_mutex);
        if (m_scanned) {
            auto it = m_index.find(relativePath);
            if (it == m_index.end()) {
                return geode::Err("local base pack has no {}", relativePath);
            }
            std::error_code ec;
            if (!std::filesystem::is_regular_file(it->second, ec) || ec) {
                return geode::Err("local file vanished: {}", relativePath);
            }
            return geode::Ok(it->second);
        }
    }
    GEODE_UNWRAP(ensureManifest());
    std::lock_guard lock(m_mutex);
    auto it = m_index.find(relativePath);
    if (it == m_index.end()) {
        return geode::Err("local base pack has no {}", relativePath);
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(it->second, ec) || ec) {
        return geode::Err("local file vanished: {}", relativePath);
    }
    return geode::Ok(it->second);
}

geode::Result<std::optional<std::filesystem::path>> LocalBasePack::ensureOptionalFile(
    std::string const&) {
    return geode::Ok(std::optional<std::filesystem::path>{std::nullopt});
}

bool LocalBasePack::snapshotTexture(cocos2d::CCTexture2D* tex, std::filesystem::path const& dst) {
    return ImageLoadHelper::saveTextureToPng(tex, dst);
}

geode::Result<int> LocalBasePack::captureLoadedSnapshots(
    std::vector<std::string> const& pngRels) {
    {
        std::lock_guard lock(m_mutex);
        m_snapshots.clear();
    }
    auto* cache = CCTextureCache::sharedTextureCache();
    if (!cache) return geode::Err("texture cache unavailable");

    int captured = 0;
    for (auto const& rel : pngRels) {
        CCTexture2D* tex = cache->textureForKey(rel.c_str());
        if (!tex) {
            // runtime keys are usually bare filenames even for mod sheets.
            auto slash = rel.find_last_of('/');
            if (slash != std::string::npos) {
                tex = cache->textureForKey(rel.substr(slash + 1).c_str());
            }
        }
        if (!tex) continue;
        auto dst = snapshotDir() / snapshotFileName(rel);
        if (snapshotTexture(tex, dst)) {
            std::lock_guard lock(m_mutex);
            m_snapshots[rel] = dst;
            ++captured;
        }
    }
    return geode::Ok(captured);
}

std::optional<std::filesystem::path> LocalBasePack::snapshotFor(
    std::string const& pngRel) const {
    std::lock_guard lock(m_mutex);
    auto it = m_snapshots.find(pngRel);
    if (it == m_snapshots.end()) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(it->second, ec) || ec) return std::nullopt;
    return it->second;
}

}
