#include "ProjectShare.hpp"

#include "SlotPaths.hpp"
#include "SlotStore.hpp"
#include "TextureProjectSerialize.hpp"
#include "../services/LocalBasePack.hpp"
#include "../services/LiveSlotRuntime.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>

#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

geode::Result<> ProjectShare::exportTo(std::filesystem::path const& dst,
                                       TextureProject const& project) {
    std::error_code ec;
    if (dst.has_parent_path()) {
        std::filesystem::create_directories(dst.parent_path(), ec);
        if (ec) {
            return Err("create_directories '{}': {}",
                geode::utils::string::pathToString(dst.parent_path()), ec.message());
        }
    }

    // same serializer as slot project.json.
    auto json = matjson::Value(project);
    auto wr = file::writeString(dst, json.dump());
    if (!wr) {
        return Err("writeString '{}': {}",
            geode::utils::string::pathToString(dst), wr.unwrapErr());
    }
    return Ok();
}

geode::Result<std::string> ProjectShare::importFrom(
    std::filesystem::path const& src) {
    auto rd = file::readJson(src);
    if (!rd) {
        return Err("readJson '{}': {}",
            geode::utils::string::pathToString(src), rd.unwrapErr());
    }
    auto parsed = rd.unwrap().as<TextureProject>();
    if (parsed.isErr()) {
        return Err("not a Texture Studio pack: {}", parsed.unwrapErr());
    }
    auto project = parsed.unwrap();

    if (project.liveRendering) {
        LiveSlotRuntime::normalize(project);
        project.sheets.clear();
        project.representativeFrame.clear();
        project.representativeSheetIndex = -1;
        project.createdAt = project.modifiedAt = nowUnixMs();
        return SlotStore::get().createSlot(project);
    }

    // foreign file: drop build state, re-resolve sheets locally.
    project.hasBuiltOnce = false;
    project.lastBuiltAt  = 0;
    project.lastZipRelPath.clear();
    project.representativeFrame.clear();
    project.representativeSheetIndex = -1;
    for (auto& sheet : project.sheets) {
        auto rel = sheet.baseName + sheet.qualitySuffix + ".png";
        auto pngPath = LocalBasePack::get().ensureFile(rel);
        if (!pngPath) {
            return Err("sheet '{}' is not installed on this game", rel);
        }
        auto plistRel = sheet.baseName + sheet.qualitySuffix + ".plist";
        auto plistPath = LocalBasePack::get().ensureFile(plistRel);
        if (!plistPath) {
            return Err("sheet '{}' is not installed on this game", plistRel);
        }
        sheet.sourcePngPath   = geode::utils::string::pathToString(*pngPath);
        sheet.sourcePlistPath = geode::utils::string::pathToString(*plistPath);
    }
    (void)ensureRepresentativeFrame(project);

    auto created = SlotStore::get().createSlot(project);
    if (!created) {
        return Err("createSlot: {}", created.unwrapErr());
    }
    return Ok(created.unwrap());
}

}  // namespace paimon::texture_studio
