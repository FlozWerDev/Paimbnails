#include "../../../utils/FilesystemPath.hpp"
#include "IconProjectStore.hpp"

#include "IconPaths.hpp"
#include "../data/IconAnatomy.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ProjectStorage.hpp"
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
    obj["favorite"]     = e.favorite;
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
    e.favorite     = v["favorite"].asBool().unwrapOr(false);
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
        recoverUnindexedProjects();
        return;
    }

    auto rd = geode::utils::file::readJson(paimon::paths::toSdkPath(path));
    if (!rd) {
        log::warn("[icon-maker] icons.json read failed: {}", rd.unwrapErr());
        recoverUnindexedProjects();
        return;
    }
    auto doc = rd.unwrap();

    if (auto arr = doc["icons"].asArray(); arr.isOk()) {
        for (auto const& entry : arr.unwrap()) {
            auto e = indexEntryFromJson(entry);
            if (paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), e.id) && !exists(e.id)) {
                m_index.push_back(std::move(e));
            }
        }
    }
    sortIndex();
    recoverUnindexedProjects();

    log::info("[icon-maker] loaded {} icon(s) from index", m_index.size());
}

void IconProjectStore::recoverUnindexedProjects() {
    auto originalCount = m_index.size();
    std::error_code ec;
    for (std::filesystem::directory_iterator it(IconPaths::slotsDir(), ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        if (!it->is_directory(typeError)) continue;
        auto id = paimon::paths::toUtf8(it->path().filename());
        if (exists(id) || !paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), id)) continue;
        auto json = geode::utils::file::readJson(paimon::paths::toSdkPath(IconPaths::projectFile(id)));
        if (!json) continue;
        auto parsed = json.unwrap().as<IconProject>();
        if (!parsed || parsed.unwrap().id != id) continue;
        auto project = parsed.unwrap();
        m_projects[id] = project;
        upsertIndexEntry(project);
    }
    if (m_index.size() != originalCount) {
        if (auto saved = saveIndex(); !saved) {
            log::warn("[icon-maker] Recovered projects; index save failed: {}", saved.unwrapErr());
        }
    }
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

    auto wr = paimon::file::writeAtomically(IconPaths::indexFile(), doc.dump());
    if (!wr) {
        return Err("Unable to atomically save icons.json");
    }
    return Ok();
}

geode::Result<> IconProjectStore::setFavorite(std::string_view id, bool favorite) {
    loadIndex();
    auto storedId = canonicalId(id);
    id = storedId;
    for (auto& entry : m_index) {
        if (entry.id != id) continue;
        entry.favorite = favorite;
        return saveIndex();
    }
    return Err("No existe ese icono");
}

std::string IconProjectStore::canonicalId(std::string_view id) const {
    auto key = paimon::storage::projectIdKey(id);
    for (auto const& entry : m_index) {
        if (paimon::storage::projectIdKey(entry.id) == key) return entry.id;
    }
    return std::string(id);
}

bool IconProjectStore::exists(std::string_view id) const {
    for (auto const& e : m_index) {
        if (paimon::storage::projectIdKey(e.id) == paimon::storage::projectIdKey(id)) return true;
    }
    return false;
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

    std::error_code reserveError;
    auto directory = paimon::storage::reserveProjectDirectory(IconPaths::slotsDir(), reserveError);
    if (directory.empty()) return Err("Unable to reserve project directory: {}", reserveError.message());
    auto finalId = paimon::paths::toUtf8(directory.filename());
    seed.id = finalId;

    auto now = nowUnixMs();
    if (seed.createdAt  == 0) seed.createdAt  = now;
    if (seed.modifiedAt == 0) seed.modifiedAt = now;

    if (auto r = IconPaths::ensureSlotDirs(finalId); !r) {
        return Err("createProject dirs: {}", r.unwrapErr());
    }

    auto json = matjson::Value(seed);
    auto wr = paimon::file::writeAtomically(IconPaths::projectFile(finalId), json.dump());
    if (!wr) {
        return Err("Unable to atomically save project");
    }

    m_projects[finalId] = seed;
    m_revisions[paimon::storage::projectIdKey(finalId)] = ++m_nextRevision;
    upsertIndexEntry(seed);

    if (auto r = saveIndex(); !r) {
        return Err("Icon saved, but index write failed: {}", r.unwrapErr());
    }

    return Ok(finalId);
}

geode::Result<IconProject> IconProjectStore::loadProject(std::string_view id) {
    loadIndex();
    auto storedId = canonicalId(id);
    id = storedId;
    if (!paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), id))
        return Err("Invalid or unconfined project ID");

    auto it = m_projects.find(std::string(id));
    if (it != m_projects.end()) {
        return Ok(it->second);
    }

    auto path = IconPaths::projectFile(id);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Err("el icono '{}' no tiene project.json", id);
    }
    auto rd = geode::utils::file::readJson(paimon::paths::toSdkPath(path));
    if (!rd) {
        return Err("readJson project.json: {}", rd.unwrapErr());
    }
    auto parsed = rd.unwrap().as<IconProject>();
    if (parsed.isErr()) {
        return Err("project.json: {}", parsed.unwrapErr());
    }
    auto p = parsed.unwrap();
    if (paimon::storage::projectIdKey(p.id) != paimon::storage::projectIdKey(id))
        return Err("Project ID does not match its directory");
    p.id = std::string(id);
    m_projects[std::string(id)] = p;
    return Ok(std::move(p));
}

geode::Result<> IconProjectStore::saveProject(IconProject const& project) {
    loadIndex();
    auto storedId = canonicalId(project.id);
    if (storedId != project.id) {
        auto normalized = project;
        normalized.id = std::move(storedId);
        return saveProject(normalized);
    }

    if (!paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), project.id))
        return Err("Invalid or unconfined project ID");

    if (auto r = IconPaths::ensureSlotDirs(project.id); !r) {
        return Err("saveProject dirs: {}", r.unwrapErr());
    }

    auto json = matjson::Value(project);
    auto wr = paimon::file::writeAtomically(IconPaths::projectFile(project.id), json.dump());
    if (!wr) {
        return Err("Unable to atomically save project");
    }

    m_projects[project.id] = project;
    m_revisions[paimon::storage::projectIdKey(project.id)] = ++m_nextRevision;
    upsertIndexEntry(project);

    if (auto r = saveIndex(); !r) {
        return Err("Icon saved, but index write failed: {}", r.unwrapErr());
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

    // copy imported images so the duplicate is fully independent.
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

std::uint64_t IconProjectStore::revision(std::string_view id) const {
    auto it = m_revisions.find(paimon::storage::projectIdKey(id));
    return it == m_revisions.end() ? 0 : it->second;
}

geode::Result<> IconProjectStore::deleteProject(std::string_view id) {
    loadIndex();
    auto storedId = canonicalId(id);
    id = storedId;
    auto directory = paimon::storage::resolveProjectDirectory(IconPaths::slotsDir(), id);
    if (!directory || !exists(id)) return Err("Invalid or unknown project ID");
    auto previousIndex = m_index;
    std::erase_if(m_index, [&](IconIndexEntry const& e) { return e.id == id; });
    if (auto result = saveIndex(); !result) {
        m_index = std::move(previousIndex);
        return result;
    }
    m_projects.erase(std::string(id));
    m_revisions.erase(paimon::storage::projectIdKey(id));
    std::error_code ec;
    std::filesystem::remove_all(*directory, ec);
    if (ec) return Err("Unable to remove project: {}", ec.message());
    return Ok();
}


void IconProjectStore::sortIndex() {
    std::sort(m_index.begin(), m_index.end(),
        [](IconIndexEntry const& a, IconIndexEntry const& b) {
            return a.modifiedAt > b.modifiedAt;
        });
}

}  // namespace paimon::icon_maker
