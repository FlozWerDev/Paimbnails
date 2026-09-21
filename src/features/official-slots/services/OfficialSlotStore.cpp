#include "OfficialSlotStore.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/string.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>

using namespace geode::prelude;

namespace paimon::officialslots {

namespace {

constexpr char const* kStoreFile = "official-slots.json";
constexpr char const* kGmdFolder = "official-slots-gmd";
constexpr size_t kMaxSlots = 60;

// A .gmd is a plist the game itself parses; anything much larger than a big
// level is not one, and we would rather refuse than copy a random 200 MB file
// into the save dir.
constexpr std::uintmax_t kMaxGmdBytes = 32ull * 1024 * 1024;

std::string newSlotId() {
    // Enough entropy for a local list. Not security relevant, so the cheap
    // clock + mt19937 pair is fine and avoids pulling in a uuid dependency.
    static std::mt19937_64 rng{static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count())};
    return fmt::format("{:016x}", rng());
}

std::string stringField(matjson::Value const& obj, char const* key) {
    return obj.contains(key) ? obj[key].asString().unwrapOr("") : "";
}

int intField(matjson::Value const& obj, char const* key, int fallback = 0) {
    if (!obj.contains(key)) return fallback;
    return static_cast<int>(obj[key].asInt().unwrapOr(fallback));
}

bool boolField(matjson::Value const& obj, char const* key, bool fallback) {
    if (!obj.contains(key)) return fallback;
    return obj[key].asBool().unwrapOr(fallback);
}

Difficulty difficultyFromInt(int value) {
    for (auto difficulty : allDifficulties()) {
        if (difficultyFace(difficulty) == value) return difficulty;
    }
    return Difficulty::Unrated;
}

Tier tierFromInt(int value) {
    switch (value) {
        case 1: return Tier::Featured;
        case 2: return Tier::Epic;
        case 3: return Tier::Legendary;
        case 4: return Tier::Mythic;
        default: return Tier::None;
    }
}

matjson::Value slotToJson(Slot const& slot) {
    return matjson::makeObject({
        {"id", slot.id},
        {"source", static_cast<int>(slot.source)},
        {"levelId", slot.levelId},
        {"gmdFile", slot.gmdFile},
        {"name", slot.name},
        {"author", slot.author},
        {"difficulty", difficultyFace(slot.difficulty)},
        {"tier", static_cast<int>(slot.tier)},
        {"stars", slot.stars},
        {"coins", slot.coins},
        {"replaces", slot.replacesOfficialId},
        {"enabled", slot.enabled},
    });
}

std::optional<Slot> slotFromJson(matjson::Value const& entry) {
    if (!entry.isObject()) return std::nullopt;

    Slot slot;
    slot.id = stringField(entry, "id");
    if (slot.id.empty()) return std::nullopt;

    slot.source = intField(entry, "source") == 1 ? Source::Gmd : Source::LevelId;
    slot.levelId = std::max(0, intField(entry, "levelId"));
    slot.gmdFile = stringField(entry, "gmdFile");

    // A slot that lost its payload can never be drawn, so drop it instead of
    // keeping a card that opens onto nothing.
    if (slot.source == Source::LevelId && slot.levelId <= 0) return std::nullopt;
    if (slot.source == Source::Gmd && slot.gmdFile.empty()) return std::nullopt;

    slot.name = stringField(entry, "name");
    slot.author = stringField(entry, "author");
    slot.difficulty = difficultyFromInt(intField(entry, "difficulty"));
    slot.tier = tierFromInt(intField(entry, "tier"));
    slot.stars = std::clamp(intField(entry, "stars"), kMinStars, kMaxStars);
    slot.coins = boolField(entry, "coins", false);

    int replaces = intField(entry, "replaces");
    slot.replacesOfficialId = isOfficialId(replaces) ? replaces : 0;
    slot.enabled = boolField(entry, "enabled", true);
    return slot;
}

} // namespace

SlotStore& SlotStore::get() {
    static SlotStore instance;
    return instance;
}

std::filesystem::path SlotStore::storePath() const {
    return Mod::get()->getSaveDir() / kStoreFile;
}

std::filesystem::path SlotStore::gmdDir() const {
    auto dir = Mod::get()->getSaveDir() / kGmdFolder;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        log::warn("[OfficialSlots] Could not create the .gmd folder: {}", ec.message());
    }
    return dir;
}

void SlotStore::ensureLoaded() {
    if (m_loaded) return;
    m_loaded = true;

    auto path = this->storePath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto contents = utils::file::readString(path);
    if (!contents) {
        log::warn("[OfficialSlots] Could not read {}: {}",
                  utils::string::pathToString(path), contents.unwrapErr());
        return;
    }

    auto parsed = matjson::parse(contents.unwrap());
    if (!parsed) {
        log::warn("[OfficialSlots] Malformed slot file: {}", parsed.unwrapErr());
        return;
    }

    auto const& root = parsed.unwrap();

    if (root.contains("slots") && root["slots"].isArray()) {
        for (auto const& entry : root["slots"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            if (auto slot = slotFromJson(entry)) {
                m_slots.push_back(std::move(*slot));
            }
        }
    }

    if (root.contains("hidden") && root["hidden"].isArray()) {
        for (auto const& entry : root["hidden"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            int id = static_cast<int>(entry.asInt().unwrapOr(0));
            if (isOfficialId(id) && !this->isOfficialHidden(id)) {
                m_hidden.push_back(id);
            }
        }
    }
}

void SlotStore::save() {
    std::vector<matjson::Value> slots;
    slots.reserve(m_slots.size());
    for (auto const& slot : m_slots) {
        slots.push_back(slotToJson(slot));
    }

    std::vector<matjson::Value> hidden;
    hidden.reserve(m_hidden.size());
    for (int id : m_hidden) {
        hidden.push_back(id);
    }

    auto root = matjson::makeObject({
        {"version", 1},
        {"slots", slots},
        {"hidden", hidden},
    });

    auto result = utils::file::writeString(this->storePath(), root.dump());
    if (!result) {
        log::warn("[OfficialSlots] Could not save the slot list: {}", result.unwrapErr());
    }
}

std::vector<Slot> const& SlotStore::slots() {
    this->ensureLoaded();
    return m_slots;
}

std::optional<Slot> SlotStore::find(std::string const& slotId) {
    this->ensureLoaded();
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                           [&](Slot const& slot) { return slot.id == slotId; });
    if (it == m_slots.end()) return std::nullopt;
    return *it;
}

std::string SlotStore::add(Slot slot) {
    this->ensureLoaded();
    if (m_slots.size() >= kMaxSlots) {
        log::warn("[OfficialSlots] Slot limit reached ({})", kMaxSlots);
        return {};
    }

    slot.id = newSlotId();
    slot.stars = std::clamp(slot.stars, kMinStars, kMaxStars);
    if (!isOfficialId(slot.replacesOfficialId)) slot.replacesOfficialId = 0;

    // Two slots on the same page would fight over one draw, so the newest wins
    // and the previous one goes back to being appended.
    if (slot.replacesOfficialId != 0) {
        for (auto& existing : m_slots) {
            if (existing.replacesOfficialId == slot.replacesOfficialId) {
                existing.replacesOfficialId = 0;
            }
        }
    }

    auto id = slot.id;
    m_slots.push_back(std::move(slot));
    this->save();
    return id;
}

bool SlotStore::update(Slot const& slot) {
    this->ensureLoaded();
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                          [&](Slot const& other) { return other.id == slot.id; });
    if (it == m_slots.end()) return false;

    Slot updated = slot;
    updated.stars = std::clamp(updated.stars, kMinStars, kMaxStars);
    if (!isOfficialId(updated.replacesOfficialId)) updated.replacesOfficialId = 0;

    if (updated.replacesOfficialId != 0) {
        for (auto& existing : m_slots) {
            if (existing.id != updated.id &&
                existing.replacesOfficialId == updated.replacesOfficialId) {
                existing.replacesOfficialId = 0;
            }
        }
    }

    // The .gmd is ours to keep only while a slot points at it.
    if (!it->gmdFile.empty() && it->gmdFile != updated.gmdFile) {
        this->discardGmd(it->gmdFile);
    }

    *it = std::move(updated);
    this->save();
    return true;
}

bool SlotStore::remove(std::string const& slotId) {
    this->ensureLoaded();
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                          [&](Slot const& slot) { return slot.id == slotId; });
    if (it == m_slots.end()) return false;

    if (!it->gmdFile.empty()) this->discardGmd(it->gmdFile);

    m_slots.erase(it);
    this->save();
    return true;
}

void SlotStore::move(std::string const& slotId, int delta) {
    this->ensureLoaded();
    if (delta == 0) return;

    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                          [&](Slot const& slot) { return slot.id == slotId; });
    if (it == m_slots.end()) return;

    auto index = static_cast<int>(std::distance(m_slots.begin(), it));
    auto target = index + delta;
    if (target < 0 || target >= static_cast<int>(m_slots.size())) return;

    std::swap(m_slots[index], m_slots[target]);
    this->save();
}

std::vector<int> const& SlotStore::hiddenOfficials() {
    this->ensureLoaded();
    return m_hidden;
}

bool SlotStore::isOfficialHidden(int levelId) {
    this->ensureLoaded();
    return std::find(m_hidden.begin(), m_hidden.end(), levelId) != m_hidden.end();
}

void SlotStore::setOfficialHidden(int levelId, bool hidden) {
    this->ensureLoaded();
    if (!isOfficialId(levelId)) return;

    auto it = std::find(m_hidden.begin(), m_hidden.end(), levelId);
    bool const already = it != m_hidden.end();
    if (hidden == already) return;

    if (hidden) {
        m_hidden.push_back(levelId);
        std::sort(m_hidden.begin(), m_hidden.end());
    } else {
        m_hidden.erase(it);
    }
    this->save();
}

std::optional<Slot> SlotStore::slotForOfficial(int levelId) {
    this->ensureLoaded();
    if (!isOfficialId(levelId)) return std::nullopt;

    for (auto const& slot : m_slots) {
        if (slot.enabled && slot.replacesOfficialId == levelId) return slot;
    }
    return std::nullopt;
}

std::optional<std::string> SlotStore::importGmd(std::filesystem::path const& source) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        log::warn("[OfficialSlots] The picked .gmd is not a file");
        return std::nullopt;
    }

    auto size = std::filesystem::file_size(source, ec);
    if (ec || size == 0 || size > kMaxGmdBytes) {
        log::warn("[OfficialSlots] Refusing a .gmd of {} bytes", ec ? 0 : size);
        return std::nullopt;
    }

    auto stem = utils::string::pathToString(source.stem());
    // The name ends up as a path, so keep it to characters that behave on every
    // platform we ship on.
    std::string safe;
    safe.reserve(stem.size());
    for (char c : stem) {
        bool const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        safe.push_back(ok ? c : '-');
    }
    if (safe.empty()) safe = "level";
    if (safe.size() > 48) safe.resize(48);

    auto fileName = fmt::format("{}-{}.gmd", safe, newSlotId());
    auto target = this->gmdDir() / fileName;

    std::filesystem::copy_file(source, target,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        log::warn("[OfficialSlots] Could not copy the .gmd: {}", ec.message());
        return std::nullopt;
    }
    return fileName;
}

void SlotStore::discardGmd(std::string const& fileName) {
    if (fileName.empty()) return;

    // Never let a stored name walk out of our folder.
    std::filesystem::path name{fileName};
    if (name.has_parent_path() || name.filename() != name) {
        log::warn("[OfficialSlots] Ignoring a suspicious .gmd name");
        return;
    }

    std::error_code ec;
    std::filesystem::remove(this->gmdDir() / name, ec);
}

} // namespace paimon::officialslots
