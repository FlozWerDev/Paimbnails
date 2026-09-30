#pragma once
// fully local base-sheet pack (idea credit: packgen); no network calls anywhere.

#include <Geode/Geode.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::texture_studio {

struct LocalBaseManifest {
    // pack-convention rels: "sheet-uhd.png" vanilla, "<modid>/<file>" mod/geode sheets.
    std::vector<std::string> files;
    // no local noscaling equivalent; kept for call-site parity, always empty.
    std::vector<std::string> noScalingFiles;

    bool empty() const { return files.empty(); }
    bool contains(std::string const& relativePath) const;
    bool isNoScaling(std::string const& relativePath) const;
};

// old network service's 3-method surface over local enumeration. sync, one background thread, mutex-guarded.
class LocalBasePack final {
public:
    static LocalBasePack& get();

    bool isManifestLoaded() const;

    // valid only after ensuremanifest succeeded (copy under lock).
    LocalBaseManifest manifest() const;

    // rebuilds the index; also deletes the orphaned packgen-cache dir once per process.
    geode::Result<> ensureManifest();

    geode::Result<std::filesystem::path> ensureFile(std::string const& relativePath);

    // no overlay variants locally: always nullopt, callers fall back to clustering.
    geode::Result<std::optional<std::filesystem::path>> ensureOptionalFile(
        std::string const& relativePath);

    // main thread only: snapshot loaded sheets so export prefers live pixels. best-effort, clears previous.
    geode::Result<int> captureLoadedSnapshots(std::vector<std::string> const& pngRels);

    // snapshot file captured for pngrel, if still on disk. thread-safe.
    std::optional<std::filesystem::path> snapshotFor(std::string const& pngRel) const;

    // main thread only: gpu pixels aren't cpu-readable, so round-trip through a render texture.
    static bool snapshotTexture(cocos2d::CCTexture2D* tex, std::filesystem::path const& dst);

private:
    LocalBasePack() = default;

    void rebuildIndexLocked();
    std::filesystem::path snapshotDir() const;

    mutable std::mutex m_mutex;
    LocalBaseManifest m_manifest;
    std::unordered_map<std::string, std::filesystem::path> m_index;
    std::unordered_map<std::string, std::filesystem::path> m_snapshots;
    bool m_scanned = false;
    bool m_cacheCleaned = false;
};

// overlay-file suffix convention (pure filename mapping, no network).
namespace packgen_suffix {
std::string overlay1(std::string const& pngRelPath);
std::string overlay2(std::string const& pngRelPath);
std::string glow(std::string const& pngRelPath);
std::string gold(std::string const& pngRelPath);
std::string demonFaces1(std::string const& pngRelPath);
std::string demonFaces2(std::string const& pngRelPath);
}  // namespace packgen_suffix

}  // namespace paimon::texture_studio
