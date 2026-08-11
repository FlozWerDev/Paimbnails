#include "IconShare.hpp"

#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"

#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include <system_error>

using namespace geode::prelude;
namespace gfile = geode::utils::file;

namespace paimon::icon_maker {

namespace {

// Keep the TaskHolder alive so Geode 5.4+ doesn't garbage-collect the pending
// pick before the OS dialog returns (same pattern as utils/FileDialog.cpp).
geode::async::TaskHolder<Result<std::optional<std::filesystem::path>>> s_pickHolder;

gfile::FilePickOptions::Filter paimbiconFilter() {
    gfile::FilePickOptions::Filter f;
    f.description = "Paimbnails Icon (*.paimbicon)";
    f.files = {"*.paimbicon"};
    return f;
}

}  // anonymous namespace

geode::Result<std::filesystem::path> IconShare::exportProject(std::string const& slotId) {
    auto projectFile = IconPaths::projectFile(slotId);
    std::error_code ec;
    if (!std::filesystem::exists(projectFile, ec)) {
        return Err("El icono '{}' no existe", slotId);
    }

    auto exportDir = IconPaths::rootDir() / "export";
    std::filesystem::create_directories(exportDir, ec);
    if (ec) return Err("create_directories: {}", ec.message());

    auto target = exportDir / (slotId + ".paimbicon");
    std::filesystem::remove(target, ec);

    GEODE_UNWRAP_INTO(auto zip, gfile::Zip::create(target));

    GEODE_UNWRAP_INTO(auto projectBytes, gfile::readBinary(projectFile));
    GEODE_UNWRAP(zip.add("project.json", projectBytes));

    auto imagesDir = IconPaths::imagesDir(slotId);
    if (std::filesystem::exists(imagesDir, ec)) {
        GEODE_UNWRAP(zip.addFolder("images"));
        std::filesystem::directory_iterator it(imagesDir, ec);
        if (!ec) {
            for (; it != std::filesystem::end(it); it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                auto name = geode::utils::string::pathToString(it->path().filename());
                auto bytes = gfile::readBinary(it->path());
                if (!bytes) continue;
                GEODE_UNWRAP(zip.add("images/" + name, bytes.unwrap()));
            }
        }
    }

    return Ok(target);
}

geode::Result<std::string> IconShare::importProject(std::filesystem::path const& file) {
    GEODE_UNWRAP_INTO(auto unzip, gfile::Unzip::create(file));

    GEODE_UNWRAP_INTO(auto projectBytes, unzip.extract("project.json"));
    std::string projectText(projectBytes.begin(), projectBytes.end());
    auto parsedJson = matjson::parse(projectText);
    if (!parsedJson) {
        return Err("project.json invalido: {}", parsedJson.unwrapErr());
    }
    auto parsed = parsedJson.unwrap().as<IconProject>();
    if (!parsed) {
        return Err("project.json invalido: {}", parsed.unwrapErr());
    }

    auto project = parsed.unwrap();
    project.id.clear();  // force a fresh, unique slot id
    project.createdAt = 0;
    project.modifiedAt = 0;
    project.hasBuiltOnce = false;
    project.lastBuiltAt = 0;

    GEODE_UNWRAP_INTO(auto newId, IconProjectStore::get().createProject(std::move(project)));

    // Extract images into the new slot.
    std::error_code ec;
    std::filesystem::create_directories(IconPaths::imagesDir(newId), ec);
    for (auto const& entry : unzip.getEntries()) {
        auto entryStr = geode::utils::string::pathToString(entry);
        constexpr std::string_view kPrefix = "images/";
        if (entryStr.rfind(kPrefix, 0) != 0) continue;
        auto name = entryStr.substr(kPrefix.size());
        if (name.empty()) continue;
        auto bytes = unzip.extract(entry);
        if (!bytes) continue;
        auto target = IconPaths::imageFile(newId, name);
        (void)gfile::writeBinary(target, bytes.unwrap());
    }

    return Ok(newId);
}

void IconShare::pickAndImport(std::function<void(geode::Result<std::string>)> callback) {
    s_pickHolder.spawn("Paimbicon Import",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {paimbiconFilter()}}),
        [callback = std::move(callback)](Result<std::optional<std::filesystem::path>> res) {
            if (!res) {
                if (callback) callback(Err(res.unwrapErr()));
                return;
            }
            auto pathOpt = res.unwrap();
            if (!pathOpt) return;  // user cancelled: stay silent
            if (callback) callback(importProject(*pathOpt));
        });
}

}  // namespace paimon::icon_maker
