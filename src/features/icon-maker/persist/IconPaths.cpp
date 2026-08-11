#include "IconPaths.hpp"

#include <Geode/utils/string.hpp>

#include <cctype>
#include <system_error>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

constexpr std::string_view kRootName    = "icon-maker";
constexpr std::string_view kIndexName   = "icons.json";
constexpr std::string_view kSlotsDir    = "slots";
constexpr std::string_view kProjectFile = "project.json";
constexpr std::string_view kImagesDir   = "images";
constexpr std::string_view kOutputDir   = "output";
constexpr std::string_view kThumbFile   = "thumb.png";

}  // anonymous namespace

std::filesystem::path IconPaths::rootDir() {
    return Mod::get()->getSaveDir() / std::string(kRootName);
}

std::filesystem::path IconPaths::indexFile() {
    return rootDir() / std::string(kIndexName);
}

std::filesystem::path IconPaths::slotsDir() {
    return rootDir() / std::string(kSlotsDir);
}

std::filesystem::path IconPaths::slotDir(std::string_view slotId) {
    return slotsDir() / std::string(slotId);
}

std::filesystem::path IconPaths::projectFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kProjectFile);
}

std::filesystem::path IconPaths::imagesDir(std::string_view slotId) {
    return slotDir(slotId) / std::string(kImagesDir);
}

std::filesystem::path IconPaths::imageFile(std::string_view slotId, std::string_view fileName) {
    return imagesDir(slotId) / sanitizeFilename(fileName);
}

std::filesystem::path IconPaths::outputDir(std::string_view slotId) {
    return slotDir(slotId) / std::string(kOutputDir);
}

std::filesystem::path IconPaths::thumbFile(std::string_view slotId) {
    return slotDir(slotId) / std::string(kThumbFile);
}

std::string IconPaths::sanitizeFilename(std::string_view name) {
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

geode::Result<> IconPaths::ensureSlotDirs(std::string_view slotId) {
    std::error_code ec;
    auto base = slotDir(slotId);

    auto mk = [&](std::filesystem::path const& p) -> geode::Result<> {
        std::filesystem::create_directories(p, ec);
        if (ec) {
            return Err("create_directories({}): {}", geode::utils::string::pathToString(p), ec.message());
        }
        return Ok();
    };

    if (auto r = mk(base);                              !r) return r;
    if (auto r = mk(base / std::string(kImagesDir));    !r) return r;
    if (auto r = mk(base / std::string(kOutputDir));    !r) return r;

    return Ok();
}

}  // namespace paimon::icon_maker
