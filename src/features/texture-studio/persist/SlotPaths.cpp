#include "../../../utils/FilesystemPath.hpp"
#include "SlotPaths.hpp"
#include "../../../utils/ProjectStorage.hpp"
#include <Geode/utils/string.hpp>

#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr std::string_view kRootName        = "texture-studio";
constexpr std::string_view kSlotsDirName    = "slots";
constexpr std::string_view kSlotsIndexName  = "slots.json";
constexpr std::string_view kProjectFile     = "project.json";
constexpr std::string_view kOverridesDir    = "overrides";
constexpr std::string_view kImagesDir       = "images";
constexpr std::string_view kFusionsDir      = "fusions";
constexpr std::string_view kCacheDir        = "cache";
constexpr std::string_view kAutoCacheFile   = "auto.bin";
constexpr std::string_view kOutputDir       = "output";
constexpr std::string_view kOutputZipFile   = "pack.zip";

}  // anonymous namespace

std::filesystem::path SlotPaths::rootDir() {
    return paimon::paths::toStdPath(Mod::get()->getSaveDir()) / std::string(kRootName);
}

std::filesystem::path SlotPaths::slotsIndexFile() {
    return rootDir() / std::string(kSlotsIndexName);
}

std::filesystem::path SlotPaths::slotsDir() {
    return rootDir() / std::string(kSlotsDirName);
}

std::filesystem::path SlotPaths::slotDir(std::string_view slotId) {
    return paimon::storage::projectDirectory(slotsDir(), slotId);
}

std::filesystem::path SlotPaths::projectFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kProjectFile);
}

std::filesystem::path SlotPaths::overridesDir(std::string_view slotId) {
    return slotDir(slotId) / std::string(kOverridesDir);
}

std::filesystem::path SlotPaths::overrideFile(std::string_view slotId,
                                              std::string_view spriteName) {
    return overridesDir(slotId) / (sanitizeFilename(spriteName) + ".bin");
}

std::filesystem::path SlotPaths::spriteImageFile(std::string_view slotId,
                                                 std::string_view spriteName) {
    return slotDir(slotId) / std::string(kImagesDir) / (sanitizeFilename(spriteName) + ".png");
}

std::filesystem::path SlotPaths::fusionsDir(std::string_view slotId) {
    return slotDir(slotId) / std::string(kFusionsDir);
}

std::filesystem::path SlotPaths::fusionMaskFile(std::string_view slotId,
                                                std::string_view spriteName) {
    return fusionsDir(slotId) / (sanitizeFilename(spriteName) + ".fus");
}

std::filesystem::path SlotPaths::fusionTextureFile(std::string_view slotId,
                                                   std::string_view spriteName,
                                                   std::string_view ext) {
    std::string e(ext);
    if (!e.empty() && e[0] != '.') e.insert(e.begin(), '.');
    return fusionsDir(slotId) / (sanitizeFilename(spriteName) + e);
}

std::optional<std::filesystem::path> SlotPaths::resolveFusionTexture(
    std::string_view slotId, std::string_view spriteName, bool preferAnimated) {
    auto gif = fusionTextureFile(slotId, spriteName, ".gif");
    auto png = fusionTextureFile(slotId, spriteName, ".png");
    std::error_code ec;
    auto const& first = preferAnimated ? gif : png;
    auto const& second = preferAnimated ? png : gif;
    if (std::filesystem::exists(first, ec)) return first;
    if (std::filesystem::exists(second, ec)) return second;
    return std::nullopt;
}

std::filesystem::path SlotPaths::autoCacheFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kCacheDir) / std::string(kAutoCacheFile);
}

std::filesystem::path SlotPaths::outputZipFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kOutputDir) / std::string(kOutputZipFile);
}

std::string SlotPaths::sanitizeFilename(std::string_view name) {
    return paimon::storage::sanitizeFilename(name);
}

geode::Result<> SlotPaths::ensureSlotDirs(std::string_view slotId) {
    if (!paimon::storage::resolveProjectDirectory(slotsDir(), slotId))
        return Err("Invalid or unconfined project ID");
    std::error_code ec;
    auto base = slotDir(slotId);

    auto mk = [&](std::filesystem::path const& p) -> geode::Result<> {
        std::filesystem::create_directories(p, ec);
        if (ec) {
            return Err("create_directories({}): {}", paimon::paths::toUtf8(p), ec.message());
        }
        return Ok();
    };

    if (auto r = mk(base);                                    !r) return r;
    if (auto r = mk(base / std::string(kOverridesDir));       !r) return r;
    if (auto r = mk(base / std::string(kImagesDir));          !r) return r;
    if (auto r = mk(base / std::string(kFusionsDir));         !r) return r;
    if (auto r = mk(base / std::string(kCacheDir));           !r) return r;
    if (auto r = mk(base / std::string(kOutputDir));          !r) return r;

    return Ok();
}

}  // namespace paimon::texture_studio
