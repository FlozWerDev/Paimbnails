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

bool isSlotKey(std::string const& key) {
    return key.size() > 2 && key[0] == 's' && key[1] == ':';
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

std::string SlotStore::officialKey(int levelId) {
    return fmt::format("o:{}", levelId);
}

std::string SlotStore::slotKey(std::string const& slotId) {
    return fmt::format("s:{}", slotId);
}

bool SlotStore::officialKeyId(std::string const& key, int& levelId) {
    if (key.size() < 3 || key[0] != 'o' || key[1] != ':') return false;
    try {
        int const parsed = std::stoi(key.substr(2));
        // Sin round-trip, "o:01" convive con "o:1" y duplica la pagina.
        if (!isOfficialId(parsed) || officialKey(parsed) != key) return false;
        levelId = parsed;
    } catch (...) {
        return false;
    }
    return true;
}

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

    this->loadOrder(root);
}

void SlotStore::loadOrder(matjson::Value const& root) {
    m_order.clear();

    auto const hasSlot = [&](std::string const& id) {
        return std::any_of(m_slots.begin(), m_slots.end(), [&](Slot const& slot) {
            return slot.id == id && slot.replacesOfficialId == 0;
        });
    };
    auto const pushOnce = [&](std::string const& key) {
        if (std::find(m_order.begin(), m_order.end(), key) == m_order.end()) {
            m_order.push_back(key);
        }
    };

    if (root.contains("order") && root["order"].isArray()) {
        for (auto const& entry : root["order"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            std::string const key = entry.asString().unwrapOr("");
            int officialId = 0;
            if (officialKeyId(key, officialId)) {
                pushOnce(key);
            } else if (isSlotKey(key) && hasSlot(key.substr(2))) {
                pushOnce(key);
            }
        }
    }

    // Anything unknown to the saved order keeps working: officials hold
    // their vanilla spot, appended slots go last in list order.
    for (int id = 1; id <= 22; ++id) pushOnce(officialKey(id));
    for (auto const& slot : m_slots) {
        if (slot.replacesOfficialId == 0) pushOnce(slotKey(slot.id));
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

    std::vector<matjson::Value> order;
    order.reserve(m_order.size());
    for (auto const& key : m_order) {
        order.push_back(key);
    }

    auto root = matjson::makeObject({
        {"version", 1},
        {"slots", slots},
        {"hidden", hidden},
        {"order", order},
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

std::string SlotStore::add(Slot slot, std::optional<std::size_t> orderIndex) {
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
                std::string const rivalKey = slotKey(existing.id);
                if (std::find(m_order.begin(), m_order.end(), rivalKey) == m_order.end()) {
                    m_order.push_back(rivalKey);
                }
            }
        }
    }

    auto id = slot.id;
    bool const appended = slot.replacesOfficialId == 0;
    m_slots.push_back(std::move(slot));
    if (appended) {
        std::string const key = slotKey(id);
        if (orderIndex && *orderIndex < m_order.size()) {
            m_order.insert(m_order.begin() + static_cast<std::ptrdiff_t>(*orderIndex), key);
        } else {
            m_order.push_back(key);
        }
    }
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
                std::string const rivalKey = slotKey(existing.id);
                if (std::find(m_order.begin(), m_order.end(), rivalKey) == m_order.end()) {
                    m_order.push_back(rivalKey);
                }
            }
        }
    }

    // The .gmd is ours to keep only while a slot points at it.
    if (!it->gmdFile.empty() && it->gmdFile != updated.gmdFile) {
        this->discardGmd(it->gmdFile);
    }

    int const wasReplacing = it->replacesOfficialId;
    *it = std::move(updated);
    std::string const key = slotKey(it->id);
    auto keyIt = std::find(m_order.begin(), m_order.end(), key);
    bool const hasKey = keyIt != m_order.end();
    if (wasReplacing != 0 && it->replacesOfficialId == 0 && !hasKey) {
        m_order.push_back(key);
    } else if (wasReplacing == 0 && it->replacesOfficialId != 0 && hasKey) {
        m_order.erase(keyIt);
    }
    this->save();
    return true;
}

bool SlotStore::remove(std::string const& slotId) {
    this->ensureLoaded();
    auto it = std::find_if(m_slots.begin(), m_slots.end(),
                          [&](Slot const& slot) { return slot.id == slotId; });
    if (it == m_slots.end()) return false;

    if (!it->gmdFile.empty()) this->discardGmd(it->gmdFile);

    std::string const key = slotKey(it->id);
    m_order.erase(std::remove(m_order.begin(), m_order.end(), key), m_order.end());

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

std::vector<std::string> const& SlotStore::pageOrder() {
    this->ensureLoaded();
    return m_order;
}

void SlotStore::movePage(std::string const& key, int delta) {
    this->ensureLoaded();
    if (delta == 0) return;

    auto it = std::find(m_order.begin(), m_order.end(), key);
    if (it == m_order.end()) return;

    auto index = static_cast<int>(std::distance(m_order.begin(), it));
    auto target = index + delta;
    if (target < 0 || target >= static_cast<int>(m_order.size())) return;

    std::swap(m_order[index], m_order[target]);
    this->save();
}

void SlotStore::movePageTo(std::string const& key, std::size_t index) {
    this->ensureLoaded();
    if (m_order.size() < 2) return;

    auto it = std::find(m_order.begin(), m_order.end(), key);
    if (it == m_order.end()) return;

    auto from = static_cast<std::size_t>(std::distance(m_order.begin(), it));
    auto target = std::min(index, m_order.size() - 1);
    if (from == target) return;

    std::string moved = std::move(*it);
    m_order.erase(it);
    m_order.insert(m_order.begin() + static_cast<std::ptrdiff_t>(std::min(target, m_order.size())),
                   std::move(moved));
    this->save();
}

bool SlotStore::pageVisible(std::string const& key) {
    int officialId = 0;
    if (officialKeyId(key, officialId)) return !this->isOfficialHidden(officialId);
    if (isSlotKey(key)) {
        if (auto slot = this->find(key.substr(2))) {
            return slot->enabled && slot->replacesOfficialId == 0;
        }
    }
    return false;
}

std::vector<std::string> SlotStore::visiblePages() {
    this->ensureLoaded();
    std::vector<std::string> visible;
    for (auto const& key : m_order) {
        if (this->pageVisible(key)) visible.push_back(key);
    }
    return visible;
}

std::size_t SlotStore::visiblePosition(std::string const& key) {
    this->ensureLoaded();
    std::size_t pos = 1;
    for (auto const& entry : m_order) {
        if (!this->pageVisible(entry)) continue;
        if (entry == key) return pos;
        ++pos;
    }
    return 0;
}

std::size_t SlotStore::orderIndexForVisiblePos(std::size_t pos) {
    this->ensureLoaded();
    if (pos < 1) return 0;
    std::size_t seen = 0;
    for (std::size_t i = 0; i < m_order.size(); ++i) {
        if (!this->pageVisible(m_order[i])) continue;
        if (++seen == pos) return i;
    }
    return m_order.size();
}

void SlotStore::movePageToVisible(std::string const& key, std::size_t pos) {
    this->ensureLoaded();
    auto it = std::find(m_order.begin(), m_order.end(), key);
    if (it == m_order.end()) return;
    std::string moved = std::move(*it);
    m_order.erase(it);
    std::size_t const index = std::min(this->orderIndexForVisiblePos(pos), m_order.size());
    m_order.insert(m_order.begin() + static_cast<std::ptrdiff_t>(index), std::move(moved));
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
