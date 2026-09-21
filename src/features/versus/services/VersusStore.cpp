#include "VersusStore.hpp"
#include "../data/VersusModes.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::versus {

namespace {

constexpr char const* kProfileKey = "versus-profiles";
constexpr char const* kHistoryKey = "versus-history";
constexpr char const* kTokenKey   = "versus-session-token";
constexpr char const* kModeKey    = "versus-mode";
constexpr char const* kFormatKeyC = "versus-format-classic";
constexpr char const* kFormatKeyP = "versus-format-platformer";
constexpr char const* kHudKey     = "versus-hud";
constexpr char const* kFriendsKey = "versus-friends-only";

int64_t intField(matjson::Value const& v, char const* key, int64_t fallback = 0) {
    if (!v.contains(key)) return fallback;
    return v[key].asInt().unwrapOr(fallback);
}

std::string stringField(matjson::Value const& v, char const* key) {
    if (!v.contains(key)) return {};
    return v[key].asString().unwrapOr("");
}

matjson::Value profileToJson(ModeProfile const& p) {
    return matjson::makeObject({
        {"elo", p.elo},
        {"best", p.best},
        {"wins", p.wins},
        {"losses", p.losses},
        {"streak", p.streak},
        {"placements", p.placementsLeft},
        {"xp", p.xpTotal},
        {"paimon", p.paimon},
        {"lastPlayed", p.lastPlayed},
    });
}

ModeProfile profileFromJson(matjson::Value const& v) {
    ModeProfile p;
    p.elo = static_cast<int>(intField(v, "elo", kStartElo));
    p.best = static_cast<int>(intField(v, "best", p.elo));
    p.wins = static_cast<int>(intField(v, "wins"));
    p.losses = static_cast<int>(intField(v, "losses"));
    p.streak = static_cast<int>(intField(v, "streak"));
    p.placementsLeft = static_cast<int>(intField(v, "placements", kPlacementMatches));
    p.xpTotal = intField(v, "xp");
    p.paimon = v.contains("paimon") && v["paimon"].asBool().unwrapOr(false);
    p.lastPlayed = intField(v, "lastPlayed");
    return p;
}

} // namespace

VersusStore& VersusStore::get() {
    static VersusStore instance;
    if (!instance.m_loaded) instance.load();
    return instance;
}

void VersusStore::load() {
    m_loaded = true;
    auto* mod = Mod::get();

    auto const raw = mod->getSavedValue<std::string>(kProfileKey, "");
    if (!raw.empty()) {
        auto parsed = matjson::parse(raw);
        if (parsed.isOk()) {
            auto const value = parsed.unwrap();
            if (value.contains("classic")) m_classic = profileFromJson(value["classic"]);
            if (value.contains("platformer")) m_platformer = profileFromJson(value["platformer"]);
        }
    }

    auto const rawHistory = mod->getSavedValue<std::string>(kHistoryKey, "");
    m_history.clear();
    if (!rawHistory.empty()) {
        auto parsed = matjson::parse(rawHistory);
        if (parsed.isOk() && parsed.unwrap().isArray()) {
            for (auto const& entry : parsed.unwrap()) {
                MatchRecord record;
                record.id = stringField(entry, "id");
                record.rival = stringField(entry, "rival");
                record.levelId = static_cast<int>(intField(entry, "level"));
                record.mode = modeFromId(stringField(entry, "mode"));
                record.format = formatFromId(stringField(entry, "format"));
                record.outcome = static_cast<Outcome>(intField(entry, "outcome"));
                record.eloDelta = static_cast<int>(intField(entry, "delta"));
                record.ownPercent = static_cast<float>(intField(entry, "own")) / 100.f;
                record.rivalPercent = static_cast<float>(intField(entry, "foe")) / 100.f;
                record.playedAt = intField(entry, "at");
                m_history.push_back(std::move(record));
            }
        }
    }

    m_token = mod->getSavedValue<std::string>(kTokenKey, "");
    m_mode = modeFromId(mod->getSavedValue<std::string>(kModeKey, "classic"));
    m_classicFormat = formatFromId(mod->getSavedValue<std::string>(kFormatKeyC, "race"));
    m_platformerFormat = formatFromId(mod->getSavedValue<std::string>(kFormatKeyP, "race"));
    m_hud = mod->getSavedValue<bool>(kHudKey, true);
    m_friendsOnly = mod->getSavedValue<bool>(kFriendsKey, false);
}

ModeProfile const& VersusStore::profile(Mode mode) const {
    return mode == Mode::Platformer ? m_platformer : m_classic;
}

void VersusStore::setProfile(Mode mode, ModeProfile const& profile) {
    (mode == Mode::Platformer ? m_platformer : m_classic) = profile;
    saveProfiles();
}

int64_t VersusStore::versusExp() const {
    return m_classic.xpTotal + m_platformer.xpTotal;
}

RankInfo VersusStore::rank(Mode mode) const {
    auto const& p = profile(mode);
    return rankFor(p.elo, p.placementsLeft, p.paimon);
}

void VersusStore::saveProfiles() {
    auto const value = matjson::makeObject({
        {"classic", profileToJson(m_classic)},
        {"platformer", profileToJson(m_platformer)},
    });
    Mod::get()->setSavedValue<std::string>(kProfileKey, value.dump(matjson::NO_INDENTATION));
}

void VersusStore::pushRecord(MatchRecord const& record) {
    m_history.insert(m_history.begin(), record);
    if (m_history.size() > kHistoryKept) m_history.resize(kHistoryKept);
    setHistory(m_history);
}

void VersusStore::setHistory(std::vector<MatchRecord> history) {
    m_history = std::move(history);
    if (m_history.size() > kHistoryKept) m_history.resize(kHistoryKept);

    std::vector<matjson::Value> entries;
    entries.reserve(m_history.size());
    for (auto const& record : m_history) {
        entries.push_back(matjson::makeObject({
            {"id", record.id},
            {"rival", record.rival},
            {"level", record.levelId},
            {"mode", modeId(record.mode)},
            {"format", formatId(record.format)},
            {"outcome", static_cast<int>(record.outcome)},
            {"delta", record.eloDelta},
            {"own", static_cast<int>(record.ownPercent * 100.f)},
            {"foe", static_cast<int>(record.rivalPercent * 100.f)},
            {"at", record.playedAt},
        }));
    }
    Mod::get()->setSavedValue<std::string>(
        kHistoryKey, matjson::Value(entries).dump(matjson::NO_INDENTATION));
}

void VersusStore::setPreferredMode(Mode mode) {
    m_mode = mode;
    Mod::get()->setSavedValue<std::string>(kModeKey, modeId(mode));
}

Format VersusStore::preferredFormat(Mode mode) const {
    Format const stored = mode == Mode::Platformer ? m_platformerFormat : m_classicFormat;

    auto const formats = queueableFormats(mode);
    for (auto const* def : formats) {
        if (def->id == stored) return stored;
    }
    return formats.empty() ? stored : formats.front()->id;
}

void VersusStore::setPreferredFormat(Mode mode, Format format) {
    if (mode == Mode::Platformer) {
        m_platformerFormat = format;
        Mod::get()->setSavedValue<std::string>(kFormatKeyP, formatId(format));
    } else {
        m_classicFormat = format;
        Mod::get()->setSavedValue<std::string>(kFormatKeyC, formatId(format));
    }
}

void VersusStore::setHudEnabled(bool enabled) {
    m_hud = enabled;
    Mod::get()->setSavedValue<bool>(kHudKey, enabled);
}

void VersusStore::setFriendsOnly(bool value) {
    m_friendsOnly = value;
    Mod::get()->setSavedValue<bool>(kFriendsKey, value);
}

void VersusStore::setSessionToken(std::string token) {
    m_token = std::move(token);
    Mod::get()->setSavedValue<std::string>(kTokenKey, m_token);
}

} // namespace paimon::versus
