#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/SafeUnzip.hpp"
#include "IconShare.hpp"

#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"

#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include <system_error>
#include <set>

using namespace geode::prelude;
namespace gfile = geode::utils::file;

namespace paimon::icon_maker {

namespace {

// keep the taskholder alive so geode 5.4+ doesn't garbage-collect the pending
// pick before the os dialog returns (same pattern as utils/filedialog.cpp).
using PickHolder =
    geode::async::TaskHolder<Result<std::optional<asp::fs::Path>>>;
// see filedialog.cpp: do not let taskholder's destructor race geode's runtime
// destructor. runtimelifecycle explicitly cancels the pending operation.
PickHolder& s_pickHolder = *new PickHolder();

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

    GEODE_UNWRAP_INTO(auto zip, gfile::Zip::create(paimon::paths::toSdkPath(target)));

    GEODE_UNWRAP_INTO(auto projectBytes, gfile::readBinary(paimon::paths::toSdkPath(projectFile)));
    GEODE_UNWRAP(zip.add("project.json", projectBytes));

    auto imagesDir = IconPaths::imagesDir(slotId);
    if (std::filesystem::exists(imagesDir, ec)) {
        GEODE_UNWRAP(zip.addFolder("images"));
        std::filesystem::directory_iterator it(imagesDir, ec);
        if (!ec) {
            for (; it != std::filesystem::end(it); it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                auto name = paimon::paths::toUtf8(it->path().filename());
                auto bytes = gfile::readBinary(paimon::paths::toSdkPath(it->path()));
                if (!bytes) continue;
                GEODE_UNWRAP(zip.add("images/" + name, bytes.unwrap()));
            }
        }
    }

    return Ok(target);
}

geode::Result<std::string> IconShare::importProject(std::filesystem::path const& file) {
    constexpr size_t kMaxProjectBytes = 2ull * 1024 * 1024;
    constexpr size_t kMaxImportBytes = 64ull * 1024 * 1024;
    constexpr size_t kMaxEntries = 512;

    std::error_code inputEc;
    auto const inputSize = std::filesystem::file_size(file, inputEc);
    if (inputEc || inputSize > kMaxImportBytes) {
        return Err("El archivo supera el limite de 64 MB");
    }

    std::string zipErr;
    paimon::zip::ZipInspectionLimits zipLimits;
    zipLimits.maxEntries = kMaxEntries;
    zipLimits.maxSingleEntryBytes = 32ull * 1024 * 1024;
    zipLimits.maxTotalUncompressedBytes = kMaxImportBytes;
    if (!paimon::zip::inspectZipFile(file, zipLimits, zipErr)) {
        return Err("{}", zipErr);
    }

    GEODE_UNWRAP_INTO(auto unzip, gfile::Unzip::create(paimon::paths::toSdkPath(file)));

    GEODE_UNWRAP_INTO(auto projectBytes, unzip.extract("project.json"));
    if (projectBytes.size() > kMaxProjectBytes) {
        return Err("project.json demasiado grande");
    }
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

    auto const entries = unzip.getEntries();
    if (entries.size() > kMaxEntries) {
        if (auto deleteRes = IconProjectStore::get().deleteProject(newId); !deleteRes) {
            log::warn("[IconShare] Could not clean up project: {}", deleteRes.unwrapErr());
        }
        return Err("El archivo contiene demasiadas imagenes");
    }

    auto failImport = [&](std::string message) -> geode::Result<std::string> {
        if (auto deleteRes = IconProjectStore::get().deleteProject(newId); !deleteRes) {
            log::warn("[IconShare] Could not clean up project: {}", deleteRes.unwrapErr());
        }
        return Err(std::move(message));
    };

    std::error_code ec;
    std::filesystem::create_directories(IconPaths::imagesDir(newId), ec);
    std::set<std::string> usedNames;
    size_t totalBytes = 0;
    for (auto const& entry : entries) {
        auto entryStr = paimon::paths::toUtf8(entry);
        constexpr std::string_view kPrefix = "images/";
        if (entryStr.rfind(kPrefix, 0) != 0) continue;
        auto name = entryStr.substr(kPrefix.size());
        if (name.empty()) continue;

        auto target = IconPaths::imageFile(newId, name);
        auto key = paimon::paths::toUtf8(target.filename());
        if (!usedNames.insert(key).second) {
            return failImport(fmt::format("Imagen duplicada en el archivo: {}", name));
        }

        auto extracted = unzip.extractTo(entry, paimon::paths::toSdkPath(target));
        if (!extracted) continue;

        std::error_code sizeEc;
        auto const size = std::filesystem::file_size(target, sizeEc);
        if (sizeEc || size > kMaxImportBytes - totalBytes) {
            std::filesystem::remove(target, sizeEc);
            return failImport("El archivo supera el limite de 64 MB");
        }
        totalBytes += size;
    }

    return Ok(newId);
}

void IconShare::pickAndImport(std::function<void(geode::Result<std::string>)> callback) {
    s_pickHolder.spawn("Paimbicon Import",
        gfile::pick(gfile::PickMode::OpenFile, {std::nullopt, {paimbiconFilter()}}),
        [callback = std::move(callback)](Result<std::optional<asp::fs::Path>> res) {
            if (!res) {
                if (callback) callback(Err(res.unwrapErr()));
                return;
            }
            auto pathOpt = res.unwrap();
            if (!pathOpt) return;  // user cancelled: stay silent
            if (callback) callback(importProject(paimon::paths::toStdPath(*pathOpt)));
        });
}

void IconShare::cancelPendingPick() {
    s_pickHolder.cancel();
}

}  // namespace paimon::icon_maker
