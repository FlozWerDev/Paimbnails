#include "IconProjectStore.hpp"

#include "IconPaths.hpp"
#include "../data/IconAnatomy.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <system_error>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

matjson::Value indexEntryToJson(IconIndexEntry const& e) {
    auto obj = matjson::Value::object();
    obj["id"]           = e.id;
    obj["name"]         = e.name;
    obj["type"]         = static_cast<int>(e.type);
    obj["modifiedAt"]   = e.modifiedAt;
    obj["createdAt"]    = e.createdAt;
    obj["hasBuiltOnce"] = e.hasBuiltOnce;
    return obj;
}

IconIndexEntry indexEntryFromJson(matjson::Value const& v) {
    IconIndexEntry e;
    e.id           = v["id"].asString().unwrapOr("");
    e.name         = v["name"].asString().unwrapOr("");
    int typeRaw    = static_cast<int>(v["type"].asInt().unwrapOr(0));
    e.type         = anatomyFor(static_cast<IconType>(typeRaw))
                       ? static_cast<IconType>(typeRaw) : IconType::Cube;
    e.modifiedAt   = v["modifiedAt"].asInt().unwrapOr(0);
    e.createdAt    = v["createdAt"].asInt().unwrapOr(0);
    e.hasBuiltOnce = v["hasBuiltOnce"].asBool().unwrapOr(false);
    return e;
}

}  // anonymous namespace

IconProjectStore& IconProjectStore::get() {
    static IconProjectStore instance;
    return instance;
}

void IconProjectStore::loadIndex() {
    if (m_indexLoaded) return;
    m_indexLoaded = true;

    auto path = IconPaths::indexFile();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }

    auto rd = file::readJson(path);
    if (!rd) {
        log::warn("[icon-maker] icons.json read failed: {}", rd.unwrapErr());
        return;
    }
    auto doc = rd.unwrap();

    if (auto arr = doc["icons"].asArray(); arr.isOk()) {
        for (auto const& entry : arr.unwrap()) {
            auto e = indexEntryFromJson(entry);
            if (!e.id.empty()) {
                m_index.push_back(std::move(e));
            }
        }
    }
    sortIndex();

    log::info("[icon-maker] loaded {} icon(s) from index", m_index.size());
}

geode::Result<> IconProjectStore::saveIndex() {
    std::error_code ec;
    std::filesystem::create_directories(IconPaths::rootDir(), ec);
    if (ec) {
        return Err("create_directories root: {}", ec.message());
    }

    auto doc = matjson::Value::object();
    auto arr = matjson::Value::array();
    for (auto const& e : m_index) arr.push(indexEntryToJson(e));
    doc["icons"] = arr;

    auto wr = file::writeString(IconPaths::indexFile(), doc.dump());
    if (!wr) {
        return Err("writeString icons.json: {}", wr.unwrapErr());
    }
    return Ok();
}

bool IconProjectStore::exists(std::string_view id) const {
    for (auto const& e : m_index) {
        if (e.id == id) return true;
    }
    return false;
}

std::string IconProjectStore::makeUniqueId(std::string desiredId) const {
    if (!exists(desiredId)) return desiredId;

    for (int i = 2; i < 1000; ++i) {
        auto candidate = desiredId + "_" + std::to_string(i);
        if (!exists(candidate)) return candidate;
    }
    return desiredId + "_" + std::to_string(nowUnixMs());
}

void IconProjectStore::upsertIndexEntry(IconProject const& project) {
    for (auto& e : m_index) {
        if (e.id == project.id) {
            e.name         = project.name;
            e.type         = project.type;
            e.modifiedAt   = project.modifiedAt;
            e.createdAt    = project.createdAt;
            e.hasBuiltOnce = project.hasBuiltOnce;
            sortIndex();
            return;
        }
    }
    IconIndexEntry e;
    e.id           = project.id;
    e.name         = project.name;
    e.type         = project.type;
    e.modifiedAt   = project.modifiedAt;
    e.createdAt    = project.createdAt;
    e.hasBuiltOnce = project.hasBuiltOnce;
    m_index.push_back(std::move(e));
    sortIndex();
}

geode::Result<std::string> IconProjectStore::createProject(IconProject seed) {
    loadIndex();

    if (seed.id.empty()) {
        seed.id = IconPaths::sanitizeFilename(seed.name);
    }
    if (seed.id.empty() || seed.id == "_unnamed") {
        seed.id = "icono";
    }
    auto finalId = makeUniqueId(seed.id);
    seed.id = finalId;

    auto now = nowUnixMs();
    if (seed.createdAt  == 0) seed.createdAt  = now;
    if (seed.modifiedAt == 0) seed.modifiedAt = now;

    if (auto r = IconPaths::ensureSlotDirs(finalId); !r) {
        return Err("createProject dirs: {}", r.unwrapErr());
    }

    auto json = matjson::Value(seed);
    auto wr = file::writeString(IconPaths::projectFile(finalId), json.dump());
    if (!wr) {
        return Err("writeString project: {}", wr.unwrapErr());
    }

    m_projects[finalId] = seed;
    upsertIndexEntry(seed);

    if (auto r = saveIndex(); !r) {
        log::warn("[icon-maker] saveIndex (createProject): {}", r.unwrapErr());
    }

    return Ok(finalId);
}

geode::Result<IconProject> IconProjectStore::loadProject(std::string_view id) {
    loadIndex();

    auto it = m_projects.find(std::string(id));
    if (it != m_projects.end()) {
        return Ok(it->second);
    }

    auto path = IconPaths::projectFile(id);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Err("el icono '{}' no tiene project.json", id);
    }
    auto rd = file::readJson(path);
    if (!rd) {
        return Err("readJson project.json: {}", rd.unwrapErr());
    }
    auto parsed = rd.unwrap().as<IconProject>();
    if (parsed.isErr()) {
        return Err("project.json: {}", parsed.unwrapErr());
    }
    auto p = parsed.unwrap();
    m_projects[std::string(id)] = p;
    return Ok(std::move(p));
}

geode::Result<> IconProjectStore::saveProject(IconProject const& project) {
    loadIndex();

    if (project.id.empty()) return Err("saveProject: id vacio");

    if (auto r = IconPaths::ensureSlotDirs(project.id); !r) {
        return Err("saveProject dirs: {}", r.unwrapErr());
    }

    auto json = matjson::Value(project);
    auto wr = file::writeString(IconPaths::projectFile(project.id), json.dump());
    if (!wr) {
        return Err("writeString project: {}", wr.unwrapErr());
    }

    m_projects[project.id] = project;
    upsertIndexEntry(project);

    if (auto r = saveIndex(); !r) {
        log::warn("[icon-maker] saveIndex (saveProject): {}", r.unwrapErr());
    }
    return Ok();
}

geode::Result<std::string> IconProjectStore::duplicateProject(std::string_view id) {
    loadIndex();

    auto loaded = loadProject(id);
    if (!loaded) return Err(loaded.unwrapErr());
    auto project = loaded.unwrap();

    auto sourceDir = IconPaths::slotDir(id);
    project.id.clear();
    project.name += " (copia)";
    project.createdAt = 0;
    project.modifiedAt = 0;
    project.hasBuiltOnce = false;
    project.lastBuiltAt = 0;

    auto created = createProject(std::move(project));
    if (!created) return created;
    auto newId = created.unwrap();

    // Copy imported images so the duplicate is fully independent.
    std::error_code ec;
    auto srcImages = sourceDir / "images";
    if (std::filesystem::exists(srcImages, ec)) {
        std::filesystem::copy(srcImages, IconPaths::imagesDir(newId),
            std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            log::warn("[icon-maker] duplicate images copy: {}", ec.message());
        }
    }
    return Ok(newId);
}

geode::Result<> IconProjectStore::deleteProject(std::string_view id) {
    loadIndex();

    m_projects.erase(std::string(id));

    m_index.erase(
        std::remove_if(m_index.begin(), m_index.end(),
            [&](IconIndexEntry const& e) { return e.id == id; }),
        m_index.end());

    // Tolerate failure (e.g. file held open); next saveIndex forgets it anyway.
    auto dir = IconPaths::slotDir(id);
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
        if (ec) {
            log::warn("[icon-maker] remove_all '{}': {}",
                geode::utils::string::pathToString(dir), ec.message());
        }
    }

    if (auto r = saveIndex(); !r) {
        log::warn("[icon-maker] saveIndex (deleteProject): {}", r.unwrapErr());
    }
    return Ok();
}

void IconProjectStore::sortIndex() {
    std::sort(m_index.begin(), m_index.end(),
        [](IconIndexEntry const& a, IconIndexEntry const& b) {
            return a.modifiedAt > b.modifiedAt;
        });
}

}  // namespace paimon::icon_maker
