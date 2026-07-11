#include "SlotPaths.hpp"
#include <Geode/utils/string.hpp>

#include <cctype>
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
    return Mod::get()->getSaveDir() / std::string(kRootName);
}

std::filesystem::path SlotPaths::slotsIndexFile() {
    return rootDir() / std::string(kSlotsIndexName);
}

std::filesystem::path SlotPaths::slotsDir() {
    return rootDir() / std::string(kSlotsDirName);
}

std::filesystem::path SlotPaths::slotDir(std::string_view slotId) {
    return slotsDir() / std::string(slotId);
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

std::filesystem::path SlotPaths::autoCacheFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kCacheDir) / std::string(kAutoCacheFile);
}

std::filesystem::path SlotPaths::outputZipFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kOutputDir) / std::string(kOutputZipFile);
}

std::string SlotPaths::sanitizeFilename(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || c == '.' || c == '-' || c == '_') {
            out += c;
        } else {
            out += '_';
        }
    }
    if (out.empty()) out = "_unnamed";
    // Defensive cap below the Windows 255-char path-component limit.
    if (out.size() > 200) out.resize(200);
    return out;
}

geode::Result<> SlotPaths::ensureSlotDirs(std::string_view slotId) {
    std::error_code ec;
    auto base = slotDir(slotId);

    auto mk = [&](std::filesystem::path const& p) -> geode::Result<> {
        std::filesystem::create_directories(p, ec);
        if (ec) {
            return Err("create_directories({}): {}", geode::utils::string::pathToString(p), ec.message());
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
