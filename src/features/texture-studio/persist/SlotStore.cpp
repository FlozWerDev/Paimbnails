#include "SlotStore.hpp"

#include "SlotPaths.hpp"
#include "TextureProjectSerialize.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

// ── matjson serialization for SlotIndexEntry ────────────────────────────

namespace {

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

// ── Singleton ───────────────────────────────────────────────────────────

SlotStore& SlotStore::get() {
    static SlotStore instance;
    return instance;
}

// ── Lifecycle ───────────────────────────────────────────────────────────

void SlotStore::loadIndex() {
    if (m_indexLoaded) return;
    m_indexLoaded = true;

    auto path = SlotPaths::slotsIndexFile();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        // Fresh install: index is empty, nothing to load.
        return;
    }

    auto rd = file::readJson(path);
    if (!rd) {
        log::warn("[texture-studio] slots.json read failed: {}", rd.unwrapErr());
        return;
    }
    auto doc = rd.unwrap();

    m_activeSlotId = doc["activeSlotId"].asString().unwrapOr("");

    if (auto arr = doc["slots"].asArray(); arr.isOk()) {
        for (auto const& entry : arr.unwrap()) {
            auto e = indexEntryFromJson(entry);
            if (!e.id.empty()) {
                m_index.push_back(std::move(e));
            }
        }
    }
    // Sort by modifiedAt descending — newest at the top of the UI.
    std::sort(m_index.begin(), m_index.end(),
        [](SlotIndexEntry const& a, SlotIndexEntry const& b) {
            return a.modifiedAt > b.modifiedAt;
        });

    log::info("[texture-studio] loaded {} slot(s) from index", m_index.size());
}

geode::Result<> SlotStore::saveIndex() {
    // Make sure the root dir exists.
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

    auto wr = file::writeString(SlotPaths::slotsIndexFile(), doc.dump());
    if (!wr) {
        return Err("writeString slots.json: {}", wr.unwrapErr());
    }
    return Ok();
}

// ── Active slot ────────────────────────────────────────────────────────

void SlotStore::setActiveSlot(std::string id) {
    if (m_activeSlotId == id) return;
    m_activeSlotId = std::move(id);
    if (auto r = saveIndex(); !r) {
        log::warn("[texture-studio] saveIndex (setActiveSlot): {}", r.unwrapErr());
    }
}

// ── CRUD ───────────────────────────────────────────────────────────────

bool SlotStore::exists(std::string_view id) const {
    for (auto const& e : m_index) {
        if (e.id == id) return true;
    }
    return false;
}

std::string SlotStore::makeUniqueId(std::string desiredId) const {
    if (!exists(desiredId)) return desiredId;

    // Append _2, _3, ... until we find a free slot.
    for (int i = 2; i < 1000; ++i) {
        auto candidate = desiredId + "_" + std::to_string(i);
        if (!exists(candidate)) return candidate;
    }
    // Pathological: fall back to a millisecond suffix.
    return desiredId + "_" + std::to_string(nowUnixMs());
}

geode::Result<std::string> SlotStore::createSlot(TextureProject seed) {
    loadIndex();

    if (seed.id.empty()) {
        return Err("SlotStore::createSlot: id is empty");
    }
    auto finalId = makeUniqueId(seed.id);
    seed.id = finalId;

    auto now = nowUnixMs();
    if (seed.createdAt  == 0) seed.createdAt  = now;
    if (seed.modifiedAt == 0) seed.modifiedAt = now;

    if (auto r = SlotPaths::ensureSlotDirs(finalId); !r) {
        return Err("createSlot dir setup: {}", r.unwrapErr());
    }

    // Write the project file.
    auto json = matjson::Value(seed);
    auto wr = file::writeString(SlotPaths::projectFile(finalId), json.dump());
    if (!wr) {
        return Err("writeString project: {}", wr.unwrapErr());
    }

    // Cache + index.
    m_projects[finalId] = seed;
    SlotIndexEntry e;
    e.id           = finalId;
    e.name         = seed.name;
    e.modifiedAt   = seed.modifiedAt;
    e.createdAt    = seed.createdAt;
    e.hasBuiltOnce = seed.hasBuiltOnce;
    m_index.insert(m_index.begin(), e);  // newest first

    if (auto r = saveIndex(); !r) {
        log::warn("[texture-studio] saveIndex (createSlot): {}", r.unwrapErr());
    }

    return Ok(finalId);
}

geode::Result<TextureProject> SlotStore::loadSlot(std::string_view id) {
    loadIndex();

    auto it = m_projects.find(std::string(id));
    if (it != m_projects.end()) {
        return Ok(it->second);
    }

    auto path = SlotPaths::projectFile(id);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return Err("slot '{}' has no project.json", id);
    }
    auto rd = file::readJson(path);
    if (!rd) {
        return Err("readJson project.json: {}", rd.unwrapErr());
    }
    auto parsed = rd.unwrap().as<TextureProject>();
    if (parsed.isErr()) {
        return Err("project.json parse error: {}", parsed.unwrapErr());
    }
    auto p = parsed.unwrap();
    m_projects[std::string(id)] = p;
    return Ok(std::move(p));
}

geode::Result<> SlotStore::saveSlot(TextureProject const& project) {
    loadIndex();

    if (project.id.empty()) return Err("saveSlot: id is empty");

    if (auto r = SlotPaths::ensureSlotDirs(project.id); !r) {
        return Err("saveSlot dir setup: {}", r.unwrapErr());
    }

    auto json = matjson::Value(project);
    auto wr = file::writeString(SlotPaths::projectFile(project.id), json.dump());
    if (!wr) {
        return Err("writeString project: {}", wr.unwrapErr());
    }

    m_projects[project.id] = project;

    // Refresh the matching index entry (or insert if new).
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
        log::warn("[texture-studio] saveIndex (saveSlot): {}", r.unwrapErr());
    }
    return Ok();
}

geode::Result<> SlotStore::deleteSlot(std::string_view id) {
    loadIndex();

    // Remove from in-memory cache.
    m_projects.erase(std::string(id));

    // Remove from index.
    m_index.erase(
        std::remove_if(m_index.begin(), m_index.end(),
            [&](SlotIndexEntry const& e) { return e.id == id; }),
        m_index.end());

    if (m_activeSlotId == id) m_activeSlotId.clear();

    // Remove the on-disk slot directory recursively. We tolerate failure
    // (e.g. file held open by another process) — the next saveIndex pass
    // forgets the slot regardless.
    auto dir = SlotPaths::slotDir(id);
    std::error_code ec;
    if (std::filesystem::exists(dir, ec)) {
        std::filesystem::remove_all(dir, ec);
        if (ec) {
            log::warn("[texture-studio] remove_all '{}': {}",
                dir.string(), ec.message());
        }
    }

    if (auto r = saveIndex(); !r) {
        log::warn("[texture-studio] saveIndex (deleteSlot): {}", r.unwrapErr());
    }
    return Ok();
}

void SlotStore::rebuildIndexCache() {
    std::sort(m_index.begin(), m_index.end(),
        [](SlotIndexEntry const& a, SlotIndexEntry const& b) {
            return a.modifiedAt > b.modifiedAt;
        });
}

}  // namespace paimon::texture_studio
