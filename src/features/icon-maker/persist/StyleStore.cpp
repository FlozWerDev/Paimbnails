#include "StyleStore.hpp"

#include "IconPaths.hpp"
#include "../data/IconProject.hpp"

#include <Geode/utils/file.hpp>
#include <matjson.hpp>

#include <algorithm>
#include <random>
#include <system_error>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

std::string makeStyleId() {
    static std::mt19937 rng{std::random_device{}()};
    static constexpr char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string out(6, 'a');
    for (auto& c : out) c = kChars[rng() % (sizeof(kChars) - 1)];
    return out;
}

}  // anonymous namespace

StyleStore& StyleStore::get() {
    static StyleStore instance;
    return instance;
}

void StyleStore::load() {
    if (m_loaded) return;
    m_loaded = true;

    auto path = IconPaths::stylesFile();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return;

    auto rd = geode::utils::file::readJson(path);
    if (!rd) {
        log::warn("[icon-maker] styles.json read failed: {}", rd.unwrapErr());
        return;
    }

    auto doc = rd.unwrap();
    if (auto arr = doc["styles"].asArray(); arr.isOk()) {
        for (auto const& entry : arr.unwrap()) {
            SavedStyle style;
            style.id   = entry["id"].asString().unwrapOr("");
            style.name = entry["name"].asString().unwrapOr("");
            style.fill = fillFromJson(entry["fill"]);
            if (!style.id.empty() && !style.name.empty()) {
                m_styles.push_back(std::move(style));
            }
        }
    }
}

geode::Result<> StyleStore::save() {
    std::error_code ec;
    std::filesystem::create_directories(IconPaths::rootDir(), ec);
    if (ec) {
        return Err("create_directories root: {}", ec.message());
    }

    auto arr = matjson::Value::array();
    for (auto const& style : m_styles) {
        auto obj = matjson::Value::object();
        obj["id"]   = style.id;
        obj["name"] = style.name;
        obj["fill"] = fillToJson(style.fill);
        arr.push(obj);
    }

    auto doc = matjson::Value::object();
    doc["styles"] = arr;

    auto wr = geode::utils::file::writeString(IconPaths::stylesFile(), doc.dump());
    if (!wr) {
        return Err("writeString styles.json: {}", wr.unwrapErr());
    }
    return Ok();
}

SavedStyle const* StyleStore::find(std::string_view id) const {
    for (auto const& style : m_styles) {
        if (style.id == id) return &style;
    }
    return nullptr;
}

geode::Result<> StyleStore::add(std::string name, FillSpec const& fill) {
    load();
    if (name.empty()) return Err("El estilo necesita un nombre");
    if (m_styles.size() >= kMaxStyles) {
        return Err("Ya tienes {} estilos guardados", kMaxStyles);
    }

    SavedStyle style;
    style.id = makeStyleId();
    style.name = std::move(name);
    style.fill = fill;
    // newest save first; usually the wanted one.
    m_styles.insert(m_styles.begin(), std::move(style));
    return save();
}

geode::Result<> StyleStore::rename(std::string_view id, std::string name) {
    load();
    if (name.empty()) return Err("El estilo necesita un nombre");
    for (auto& style : m_styles) {
        if (style.id != id) continue;
        style.name = std::move(name);
        return save();
    }
    return Err("No existe ese estilo");
}

geode::Result<> StyleStore::remove(std::string_view id) {
    load();
    auto it = std::remove_if(m_styles.begin(), m_styles.end(),
        [id](SavedStyle const& style) { return style.id == id; });
    if (it == m_styles.end()) return Err("No existe ese estilo");
    m_styles.erase(it, m_styles.end());
    return save();
}

}  // namespace paimon::icon_maker
