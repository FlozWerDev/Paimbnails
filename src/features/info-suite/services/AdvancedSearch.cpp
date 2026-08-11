#include "AdvancedSearch.hpp"
#include "SearchObjectBuilder.hpp"
#include "../../../utils/JsonHelper.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <fstream>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr int kMaxPresets = 40;

std::string g_refineKey;
std::optional<AdvancedQuery> g_refine;

std::vector<SearchPreset>& presetList() {
    static std::vector<SearchPreset> list;
    return list;
}

bool g_presetsLoaded = false;

std::filesystem::path presetPath() {
    return Mod::get()->getSaveDir() / "info_search_presets.json";
}

std::string joinInts(std::vector<int> const& values, char const* fallback) {
    if (values.empty()) return fallback;
    std::string out;
    for (size_t i = 0; i < values.size(); i++) {
        if (i > 0) out.push_back(',');
        out += std::to_string(values[i]);
    }
    return out;
}

int64_t asInt(matjson::Value const& value) {
    auto res = value.asInt();
    return res.isOk() ? res.unwrap() : 0;
}

bool asBool(matjson::Value const& value) {
    auto res = value.asBool();
    return res.isOk() ? res.unwrap() : false;
}

std::string asString(matjson::Value const& value) {
    auto res = value.asString();
    return res.isOk() ? res.unwrap() : std::string{};
}

matjson::Value queryToJson(AdvancedQuery const& q) {
    auto obj = matjson::Value::object();
    obj["query"] = q.query;

    auto diffs = matjson::Value::array();
    for (int d : q.difficulties) diffs.push(d);
    obj["difficulties"] = diffs;

    auto lens = matjson::Value::array();
    for (int l : q.lengths) lens.push(l);
    obj["lengths"] = lens;

    obj["demon"] = q.demonFilter;
    obj["platformer"] = q.platformer;
    obj["star"] = q.star;
    obj["noStar"] = q.noStar;
    obj["featured"] = q.featured;
    obj["epic"] = q.epic;
    obj["legendary"] = q.legendary;
    obj["mythic"] = q.mythic;
    obj["original"] = q.original;
    obj["twoPlayer"] = q.twoPlayer;
    obj["coins"] = q.coins;
    obj["completed"] = q.completed;
    obj["uncompleted"] = q.uncompleted;
    obj["songID"] = q.songID;
    obj["songFilter"] = q.songFilter;
    obj["minID"] = q.minID;
    obj["maxID"] = q.maxID;
    obj["minGameVersion"] = q.minGameVersion;
    obj["maxGameVersion"] = q.maxGameVersion;
    obj["minObjects"] = q.minObjects;
    obj["maxObjects"] = q.maxObjects;
    return obj;
}

AdvancedQuery queryFromJson(matjson::Value const& obj) {
    AdvancedQuery q;
    q.query = asString(obj["query"]);

    paimon::json::forEachInArray(obj["difficulties"], [&](matjson::Value const& v) {
        q.difficulties.push_back(static_cast<int>(asInt(v)));
    });
    paimon::json::forEachInArray(obj["lengths"], [&](matjson::Value const& v) {
        q.lengths.push_back(static_cast<int>(asInt(v)));
    });

    q.demonFilter = static_cast<int>(asInt(obj["demon"]));
    q.platformer = asBool(obj["platformer"]);
    q.star = asBool(obj["star"]);
    q.noStar = asBool(obj["noStar"]);
    q.featured = asBool(obj["featured"]);
    q.epic = asBool(obj["epic"]);
    q.legendary = asBool(obj["legendary"]);
    q.mythic = asBool(obj["mythic"]);
    q.original = asBool(obj["original"]);
    q.twoPlayer = asBool(obj["twoPlayer"]);
    q.coins = asBool(obj["coins"]);
    q.completed = asBool(obj["completed"]);
    q.uncompleted = asBool(obj["uncompleted"]);
    q.songID = static_cast<int>(asInt(obj["songID"]));
    q.songFilter = asBool(obj["songFilter"]);
    q.minID = static_cast<int>(asInt(obj["minID"]));
    q.maxID = static_cast<int>(asInt(obj["maxID"]));
    q.minGameVersion = static_cast<int>(asInt(obj["minGameVersion"]));
    q.maxGameVersion = static_cast<int>(asInt(obj["maxGameVersion"]));
    q.minObjects = static_cast<int>(asInt(obj["minObjects"]));
    q.maxObjects = static_cast<int>(asInt(obj["maxObjects"]));
    return q;
}

void loadPresets() {
    if (g_presetsLoaded) return;
    g_presetsLoaded = true;

    auto path = presetPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();
    if (contents.empty()) return;

    auto parsed = matjson::parse(contents);
    if (!parsed.isOk()) {
        log::warn("[Paimbnails] info_search_presets.json corrupto, se ignora");
        return;
    }

    paimon::json::forEachInArray(parsed.unwrap()["presets"], [&](matjson::Value const& item) {
        SearchPreset preset;
        preset.name = asString(item["name"]);
        preset.query = queryFromJson(item["query"]);
        if (!preset.name.empty()) presetList().push_back(std::move(preset));
    });
}

void savePresets() {
    auto root = matjson::Value::object();
    auto list = matjson::Value::array();
    for (auto const& preset : presetList()) {
        auto item = matjson::Value::object();
        item["name"] = preset.name;
        item["query"] = queryToJson(preset.query);
        list.push(item);
    }
    root["presets"] = list;

    std::ofstream file(presetPath(), std::ios::trunc);
    if (!file.is_open()) {
        log::warn("[Paimbnails] no se pudo escribir info_search_presets.json");
        return;
    }
    file << root.dump(matjson::NO_INDENTATION);
}

} // namespace

bool hasRefine(AdvancedQuery const& q) {
    return q.minID > 0 || q.maxID > 0
        || q.minGameVersion > 0 || q.maxGameVersion > 0
        || q.minObjects > 0 || q.maxObjects > 0;
}

bool passesRefine(AdvancedQuery const& q, GJGameLevel* level) {
    if (!level) return false;

    int id = level->m_levelID.value();
    if (q.minID > 0 && id < q.minID) return false;
    if (q.maxID > 0 && id > q.maxID) return false;

    int version = level->m_gameVersion;
    if (q.minGameVersion > 0 && version < q.minGameVersion) return false;
    if (q.maxGameVersion > 0 && version > q.maxGameVersion) return false;

    // Object count is 0 on levels the game has not downloaded yet. Dropping
    // those would empty the list on a fresh search, so an unknown count only
    // fails when a minimum was asked for.
    int objects = level->m_objectCount.value();
    if (q.minObjects > 0 && objects < q.minObjects) return false;
    if (q.maxObjects > 0 && objects > 0 && objects > q.maxObjects) return false;

    return true;
}

GJSearchObject* buildSearch(AdvancedQuery const& q) {
    SearchFilters filters;
    filters.query = q.query;
    filters.difficulty = joinInts(q.difficulties, "-1");

    if (q.lengths.empty()) {
        filters.length = q.platformer ? std::to_string(GJGameLevel::getLengthKey(0, true)) : "-1";
    } else {
        std::vector<int> keys;
        keys.reserve(q.lengths.size());
        for (int len : q.lengths) keys.push_back(GJGameLevel::getLengthKey(len, q.platformer));
        filters.length = joinInts(keys, "-1");
    }

    filters.star = q.star;
    filters.noStar = q.noStar;
    filters.featured = q.featured;
    filters.epic = q.epic;
    filters.legendary = q.legendary;
    filters.mythic = q.mythic;
    filters.original = q.original;
    filters.twoPlayer = q.twoPlayer;
    filters.coins = q.coins;
    filters.onlyCompleted = q.completed;
    filters.uncompleted = q.uncompleted;
    filters.demonFilter = q.demonFilter;
    filters.songID = q.songID;
    filters.songFilter = q.songFilter;
    filters.customSong = q.songFilter && q.songID > 0;

    return buildSearchObject(SearchType::Search, filters);
}

void armRefine(std::string key, AdvancedQuery query) {
    g_refineKey = std::move(key);
    g_refine = std::move(query);
}

AdvancedQuery const* refineFor(std::string const& key) {
    if (!g_refine || g_refineKey.empty()) return nullptr;
    if (g_refineKey != key) return nullptr;
    return &*g_refine;
}

void clearRefine() {
    g_refineKey.clear();
    g_refine.reset();
}

void runSearch(AdvancedQuery const& query) {
    auto* obj = buildSearch(query);
    if (!obj) return;

    if (hasRefine(query)) {
        armRefine(searchKey(obj), query);
    } else {
        clearRefine();
    }

    pushBrowser(obj);
}

std::vector<SearchPreset> const& presets() {
    loadPresets();
    return presetList();
}

void addPreset(std::string name, AdvancedQuery query) {
    loadPresets();
    if (name.empty()) return;

    auto& list = presetList();
    auto existing = std::find_if(list.begin(), list.end(),
        [&](SearchPreset const& preset) { return preset.name == name; });

    if (existing != list.end()) {
        existing->query = std::move(query);
    } else {
        if (list.size() >= kMaxPresets) list.erase(list.begin());
        list.push_back({std::move(name), std::move(query)});
    }
    savePresets();
}

void removePreset(int index) {
    loadPresets();
    auto& list = presetList();
    if (index < 0 || index >= static_cast<int>(list.size())) return;
    list.erase(list.begin() + index);
    savePresets();
}

std::string describeQuery(AdvancedQuery const& q) {
    std::vector<std::string> parts;

    if (!q.query.empty()) parts.push_back(fmt::format("\"{}\"", q.query));
    if (!q.difficulties.empty()) parts.push_back(fmt::format("dif {}", joinInts(q.difficulties, "-")));
    if (!q.lengths.empty()) parts.push_back(fmt::format("len {}", joinInts(q.lengths, "-")));
    if (q.platformer) parts.push_back("plat");
    if (q.star) parts.push_back("rated");
    if (q.noStar) parts.push_back("sin estrellas");
    if (q.featured) parts.push_back("featured");
    if (q.epic) parts.push_back("epic");
    if (q.legendary) parts.push_back("legendary");
    if (q.mythic) parts.push_back("mythic");
    if (q.original) parts.push_back("original");
    if (q.twoPlayer) parts.push_back("2p");
    if (q.coins) parts.push_back("monedas");
    if (q.completed) parts.push_back("completados");
    if (q.uncompleted) parts.push_back("sin completar");
    if (q.songFilter && q.songID > 0) parts.push_back(fmt::format("song {}", q.songID));

    if (q.minID > 0 || q.maxID > 0) {
        parts.push_back(fmt::format("id {}-{}",
            q.minID > 0 ? std::to_string(q.minID) : "",
            q.maxID > 0 ? std::to_string(q.maxID) : ""));
    }
    if (q.minGameVersion > 0 || q.maxGameVersion > 0) {
        parts.push_back(fmt::format("ver {}-{}",
            q.minGameVersion > 0 ? std::to_string(q.minGameVersion) : "",
            q.maxGameVersion > 0 ? std::to_string(q.maxGameVersion) : ""));
    }
    if (q.minObjects > 0 || q.maxObjects > 0) {
        parts.push_back(fmt::format("obj {}-{}",
            q.minObjects > 0 ? std::to_string(q.minObjects) : "",
            q.maxObjects > 0 ? std::to_string(q.maxObjects) : ""));
    }

    if (parts.empty()) return "Sin filtros";

    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) out += ", ";
        out += parts[i];
    }
    return out;
}

} // namespace paimon::info
