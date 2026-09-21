#pragma once
// Fully local base-sheet pack (idea credit: PackGen); no network calls anywhere.

#include <Geode/Geode.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::texture_studio {

struct LocalBaseManifest {
    // Relative paths using pack convention: "Sheet-uhd.png" for vanilla,
    // "<modid>/<file>" for mod/Geode sheets (matches SheetRetarget::locate).
    std::vector<std::string> files;
    // No local equivalent of the old noscaling list; kept for call-site
    // parity and always empty.
    std::vector<std::string> noScalingFiles;

    bool empty() const { return files.empty(); }
    bool contains(std::string const& relativePath) const;
    bool isNoScaling(std::string const& relativePath) const;
};

// Same 3-method surface as the old network asset service, backed only by
// local directory enumeration. All methods are synchronous and safe to call
// from any single background thread; internal state is mutex-guarded.
class LocalBasePack final {
public:
    static LocalBasePack& get();

    bool isManifestLoaded() const;

    // Valid only after ensureManifest succeeded (copy under lock).
    LocalBaseManifest manifest() const;

    // Rebuilds the local index. Also deletes the orphaned
    // <saveDir>/texture-studio/packgen-cache/ directory left by older
    // builds, once per process.
    geode::Result<> ensureManifest();

    // Resolves a manifest file to its installed path.
    geode::Result<std::filesystem::path> ensureFile(std::string const& relativePath);

    // Local packs ship no overlay variants, so this always succeeds with
    // nullopt and callers fall back to clustering.
    geode::Result<std::optional<std::filesystem::path>> ensureOptionalFile(
        std::string const& relativePath);

    // Main thread ONLY: snapshot already-loaded sheet textures into files so
    // the export thread can prefer live (remapped) pixels over disk files.
    // pngRels use manifest convention ("Sheet-uhd.png", "<modid>/<file>").
    // Best-effort: unloaded sheets are skipped. Clears previous snapshots.
    geode::Result<int> captureLoadedSnapshots(std::vector<std::string> const& pngRels);

    // Snapshot file captured for pngRel, if still on disk. Thread-safe.
    std::optional<std::filesystem::path> snapshotFor(std::string const& pngRel) const;

    // Main thread ONLY: GPU pixels are not CPU-readable, so the texture is
    // round-tripped through a render texture into a PNG file.
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

// Overlay-file suffix convention (pure filename mapping, no network).
namespace packgen_suffix {
std::string overlay1(std::string const& pngRelPath);
std::string overlay2(std::string const& pngRelPath);
std::string glow(std::string const& pngRelPath);
std::string gold(std::string const& pngRelPath);
std::string demonFaces1(std::string const& pngRelPath);
std::string demonFaces2(std::string const& pngRelPath);
}  // namespace packgen_suffix

}  // namespace paimon::texture_studio
