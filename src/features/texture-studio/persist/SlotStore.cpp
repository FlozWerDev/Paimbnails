#include "../../../utils/FilesystemPath.hpp"
#include "SlotStore.hpp"

#include "SlotPaths.hpp"
#include "TextureProjectSerialize.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ProjectStorage.hpp"
#include <matjson.hpp>

#include <algorithm>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

geode::Result<> ensureProjectDirectory(TextureProject const& project) {
    if (!project.liveRendering) return SlotPaths::ensureSlotDirs(project.id);
    std::error_code ec;
    std::filesystem::create_directories(SlotPaths::slotDir(project.id), ec);
    if (ec) return Err("create slot directory: {}", ec.message());
    return Ok();
}

matjson::Value indexEntryToJson(SlotIndexEntry const& e) {
    auto obj = matjson::Value::object();
    obj["id"]            = e.id;
    obj["name"]          = e.name;
    obj["modifiedAt"]    = e.modifiedAt;
    obj["createdAt"]     = e.createdAt;
    obj["hasBuiltOnce"]  = e.hasBuiltOnce;
    return obj;
}

SlotIndexEntry indexEntryFromJson(matjson::Value const& v) {
    SlotIndexEntry e;
    e.id           = v["id"].asString().unwrapOr("");
    e.name         = v["name"].asString().unwrapOr("");
    e.modifiedAt   = static_cast<std::int64_t>(v["modifiedAt"].asInt().unwrapOr(0));
    e.createdAt    = static_cast<std::int64_t>(v["createdAt"].asInt().unwrapOr(0));
    e.hasBuiltOnce = v["hasBuiltOnce"].asBool().unwrapOr(false);
    return e;
}

}  // anonymous namespace

SlotStore& SlotStore::get() {
    static SlotStore instance;
    return instance;
}

void SlotStore::loadIndex() {
    if (m_indexLoaded) return;
    m_indexLoaded = true;

    auto path = SlotPaths::slotsIndexFile();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        recoverUnindexedProjects();
        return;
    }

    auto rd = geode::utils::file::readJson(paimon::paths::toSdkPath(path));
    if (!rd) {
        log::warn("[texture-studio] slots.json read failed: {}", rd.unwrapErr());
        recoverUnindexedProjects();
        return;
    }
    auto doc = rd.unwrap();

    m_activeSlotId = doc["activeSlotId"].asString().unwrapOr("");
    if (!paimon::storage::resolveProjectDirectory(SlotPaths::slotsDir(), m_activeSlotId)) m_activeSlotId.clear();

    if (auto arr = doc["slots"].asArray(); arr.isOk()) {
        for (auto const& entry : arr.unwrap()) {
            auto e = indexEntryFromJson(entry);
            if (paimon::storage::resolveProjectDirectory(SlotPaths::slotsDir(), e.id) && !exists(e.id)) {
                m_index.push_back(std::move(e));
            }
        }
    }
    std::sort(m_index.begin(), m_index.end(),
        [](SlotIndexEntry const& a, SlotIndexEntry const& b) {
            return a.modifiedAt > b.modifiedAt;
        });
    recoverUnindexedProjects();

    log::info("[texture-studio] loaded {} slot(s) from index", m_index.size());
}

void SlotStore::recoverUnindexedProjects() {
    auto originalCount = m_index.size();
    std::error_code ec;
    for (std::filesystem::directory_iterator it(SlotPaths::slotsDir(), ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code typeError;
        if (!it->is_directory(typeError)) continue;
        auto id = paimon::paths::toUtf8(it->path().filename());
        if (exists(id) || !paimon::storage::resolveProjectDirectory(SlotPaths::slotsDir(), id)) continue;
        auto json = geode::utils::file::readJson(paimon::paths::toSdkPath(SlotPaths::projectFile(id)));
        if (!json) continue;
        auto parsed = json.unwrap().as<TextureProject>();
        if (!parsed || parsed.unwrap().id != id) continue;
        auto project = parsed.unwrap();
        m_projects[id] = project;
        m_index.push_back({id, project.name, project.modifiedAt, project.createdAt, project.hasBuiltOnce});
    }
    if (m_index.size() != originalCount) {
        rebuildIndexCache();
        if (auto saved = saveIndex(); !saved) {
            log::warn("[texture-studio] Recovered projects; index save failed: {}", saved.unwrapErr());
        }
    }
}

geode::Result<> SlotStore::saveIndex() {
    std::error_code ec;
    std::filesystem::create_directories(SlotPaths::rootDir(), ec);
    if (ec) {
        return Err("create_directories root: {}", ec.message());
    }

    auto doc = matjson::Value::object();
    doc["activeSlotId"] = m_activeSlotId;
    auto arr = matjson::Value::array();
    for (auto const& e : m_index) arr.push(indexEntryToJson(e));
    doc["slots"] = arr;

    auto wr = paimon::file::writeAtomically(SlotPaths::slotsIndexFile(), doc.dump());
    if (!wr) {
        return Err("Unable to atomically save slots.json");
    }
    return Ok();
}

geode::Result<> SlotStore::setActiveSlot(std::string id) {
    loadIndex();
    id = canonicalId(id);
    if (!id.empty() && !exists(id)) return Err("Slot does not exist");
    if (m_activeSlotId == id) return Ok();
    auto previous = m_activeSlotId;
    m_activeSlotId = std::move(id);
    if (auto r = saveIndex(); !r) {
        m_activeSlotId = std::move(previous);
        return r;
    }
    return Ok();
}

std::string SlotStore::canonicalId(std::string_view id) const {
    auto key = paimon::storage::projectIdKey(id);
    for (auto const& entry : m_index) {
        if (paimon::storage::projectIdKey(entry.id) == key) return entry.id;
    }
    return std::string(id);
}

bool SlotStore::exists(std::string_view id) const {
    for (auto const& e : m_index) {
        if (paimon::storage::projectIdKey(e.id) == paimon::storage::projectIdKey(id)) return true;
    }
    return false;
}


geode::Result<std::string> SlotStore::createSlot(TextureProject seed) {
    loadIndex();

    std::error_code reserveError;
    auto directory = paimon::storage::reserveProjectDirectory(SlotPaths::slotsDir(), reserveError);
    if (directory.empty()) return Err("Unable to reserve project directory: {}", reserveError.message());
    auto finalId = paimon::paths::toUtf8(directory.filename());
    seed.id = finalId;

    auto now = nowUnixMs();
    if (seed.createdAt  == 0) seed.createdAt  = now;
    if (seed.modifiedAt == 0) seed.modifiedAt = now;

    if (auto r = ensureProjectDirectory(seed); !r) {
        return Err("createSlot dir setup: {}", r.unwrapErr());
    }

    auto json = matjson::Value(seed);
    auto wr = paimon::file::writeAtomically(SlotPaths::projectFile(finalId), json.dump());
    if (!wr) {
        return Err("Unable to atomically save project");
    }

    m_projects[finalId] = seed;
    SlotIndexEntry e;
    e.id           = finalId;
    e.name         = seed.name;
    e.modifiedAt   = seed.modifiedAt;
    e.createdAt    = seed.createdAt;
    e.hasBuiltOnce = seed.hasBuiltOnce;
    m_index.insert(m_index.begin(), e);

    if (auto r = saveIndex(); !r) {
        return Err("Slot saved, but index write failed: {}", r.unwrapErr());
    }

    return Ok(finalId);
}

geode::Result<TextureProject> SlotStore::loadSlot(std::string_view id) {
    loadIndex();
    auto storedId = canonicalId(id);
    id = storedId;
    if (!paimon::storage::resolveProjectDirectory(SlotPaths::slotsDir(), id))
        return Err("Invalid or unconfined project ID");

    auto it = m_projects.find(std::string(id));
    if (it != m_projects.end()) {
        return Ok(it->second);
    }

    auto path = SlotPaths::projectFile(id);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Err("slot '{}' has no project.json", id);
    }
    auto rd = geode::utils::file::readJson(paimon::paths::toSdkPath(path));
    if (!rd) {
        return Err("readJson project.json: {}", rd.unwrapErr());
    }
    auto parsed = rd.unwrap().as<TextureProject>();
    if (parsed.isErr()) {
        return Err("project.json parse error: {}", parsed.unwrapErr());
    }
    auto p = parsed.unwrap();
    if (paimon::storage::projectIdKey(p.id) != paimon::storage::projectIdKey(id))
        return Err("Project ID does not match its directory");
    p.id = std::string(id);
    m_projects[std::string(id)] = p;
    return Ok(std::move(p));
}

geode::Result<> SlotStore::saveSlot(TextureProject const& project) {
    loadIndex();
    auto storedId = canonicalId(project.id);
    if (storedId != project.id) {
        auto normalized = project;
        normalized.id = std::move(storedId);
        return saveSlot(normalized);
    }

    if (!paimon::storage::resolveProjectDirectory(SlotPaths::slotsDir(), project.id))
        return Err("Invalid or unconfined project ID");

    if (auto r = ensureProjectDirectory(project); !r) {
        return Err("saveSlot dir setup: {}", r.unwrapErr());
    }

    auto json = matjson::Value(project);
    auto wr = paimon::file::writeAtomically(SlotPaths::projectFile(project.id), json.dump());
    if (!wr) {
        return Err("Unable to atomically save project");
    }

    m_projects[project.id] = project;

    bool found = false;
    for (auto& e : m_index) {
        if (e.id == project.id) {
            e.name         = project.name;
            e.modifiedAt   = project.modifiedAt;
            e.createdAt    = project.createdAt;
            e.hasBuiltOnce = project.hasBuiltOnce;
            found = true;
            break;
        }
    }
    if (!found) {
        SlotIndexEntry e;
        e.id           = project.id;
        e.name         = project.name;
        e.modifiedAt   = project.modifiedAt;
        e.createdAt    = project.createdAt;
        e.hasBuiltOnce = project.hasBuiltOnce;
        m_index.push_back(std::move(e));
    }
    rebuildIndexCache();
    if (auto r = saveIndex(); !r) {
        return Err("Slot saved, but index write failed: {}", r.unwrapErr());
    }
    return Ok();
}

geode::Result<> SlotStore::deleteSlot(std::string_view id) {
    loadIndex();
    auto storedId = canonicalId(id);
    id = storedId;
    auto directory = paimon::storage::resolveProjectDirectory(SlotPaths::slotsDir(), id);
    if (!directory || !exists(id)) return Err("Invalid or unknown project ID");
    auto previousIndex = m_index;
    auto previousActive = m_activeSlotId;
    std::erase_if(m_index, [&](SlotIndexEntry const& entry) { return entry.id == id; });
    if (m_activeSlotId == id) m_activeSlotId.clear();
    if (auto result = saveIndex(); !result) {
        m_index = std::move(previousIndex);
        m_activeSlotId = std::move(previousActive);
        return result;
    }
    m_projects.erase(std::string(id));
    std::error_code ec;
    std::filesystem::remove_all(*directory, ec);
    if (ec) return Err("Unable to remove slot: {}", ec.message());
    return Ok();
}

void SlotStore::rebuildIndexCache() {
    std::sort(m_index.begin(), m_index.end(),
        [](SlotIndexEntry const& a, SlotIndexEntry const& b) {
            return a.modifiedAt > b.modifiedAt;
        });
}

}  // namespace paimon::texture_studio
