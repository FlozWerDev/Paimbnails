#include "ObjectFavoritesStore.hpp"

#include <Geode/loader/Mod.hpp>
#include <algorithm>
#include <matjson.hpp>

using namespace geode::prelude;

namespace paimon::editor {

namespace {
constexpr char const* kKey = "paim-editor-object-favorites";
}

ObjectFavoritesStore& ObjectFavoritesStore::get() {
    static ObjectFavoritesStore s;
    if (!s.m_loaded) s.load();
    return s;
}

void ObjectFavoritesStore::load() {
    m_ids.clear();
    m_loaded = true;
    auto raw = Mod::get()->getSavedValue<std::string>(kKey, "[]");
    auto parsed = matjson::parse(raw);
    if (parsed.isErr()) return;
    auto arr = parsed.unwrap().asArray();
    if (arr.isErr()) return;
    for (auto const& v : arr.unwrap()) {
        auto n = v.asInt();
        if (n.isOk()) {
            int id = static_cast<int>(n.unwrap());
            if (id > 0) m_ids.push_back(id);
        }
    }
    std::sort(m_ids.begin(), m_ids.end());
    m_ids.erase(std::unique(m_ids.begin(), m_ids.end()), m_ids.end());
}

void ObjectFavoritesStore::save() const {
    auto arr = matjson::Value::array();
    for (int id : m_ids) arr.push(id);
    Mod::get()->setSavedValue(kKey, arr.dump());
}

bool ObjectFavoritesStore::isFavorite(int objectId) const {
    return std::binary_search(m_ids.begin(), m_ids.end(), objectId);
}

void ObjectFavoritesStore::add(int objectId) {
    if (objectId <= 0 || isFavorite(objectId)) return;
    m_ids.push_back(objectId);
    std::sort(m_ids.begin(), m_ids.end());
    save();
}

void ObjectFavoritesStore::remove(int objectId) {
    auto it = std::lower_bound(m_ids.begin(), m_ids.end(), objectId);
    if (it != m_ids.end() && *it == objectId) {
        m_ids.erase(it);
        save();
    }
}

bool ObjectFavoritesStore::toggle(int objectId) {
    if (isFavorite(objectId)) {
        remove(objectId);
        return false;
    }
    add(objectId);
    return true;
}

void ObjectFavoritesStore::clear() {
    m_ids.clear();
    save();
}

} // namespace paimon::editor
