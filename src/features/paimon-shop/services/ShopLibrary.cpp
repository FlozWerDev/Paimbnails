#include "ShopLibrary.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/BoundedFileRead.hpp"

#include <algorithm>
#include <chrono>
#include <random>

namespace paimon::shop {
namespace {
bool validId(std::string const& id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == std::string::npos;
}

geode::Result<matjson::Value> readJson(std::filesystem::path const& path, size_t limit) {
    auto bytes = paimon::file::readBytes(path, limit);
    if (bytes.empty()) return geode::Err("No se pudo leer el archivo / Could not read file");
    auto parsed = parseBoundedJson(std::string_view(reinterpret_cast<char const*>(bytes.data()), bytes.size()));
    if (!parsed) return geode::Err("JSON invalido / Invalid JSON");
    return geode::Ok(parsed.unwrap());
}

std::string makeId() {
    static std::mt19937_64 random{std::random_device{}()};
    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i) id += hex[random() & 15];
    return id;
}
}

geode::Result<matjson::Value> ShopLibrary::readIndex() const {
    auto path = m_directory / "index.json";
    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);
    if (ec) return geode::Err("No se pudo abrir la biblioteca / Could not open library");
    if (!exists) return geode::Ok(matjson::Value::array());
    auto root = readJson(path, 512 * 1024);
    if (!root || !root.unwrap().isArray() || root.unwrap().size() > kMaxLibraryEntries) {
        return geode::Err("Indice local danado / Local index is damaged");
    }
    for (auto const& row : root.unwrap()) {
        if (!validId(row["id"].asString().unwrapOr("")) || !parseCategory(row["category"].asString().unwrapOr(""))) {
            return geode::Err("Indice local invalido / Invalid local index");
        }
    }
    return root;
}

geode::Result<std::vector<Listing>> ShopLibrary::list() const {
    auto index = readIndex();
    if (!index) return geode::Err(index.unwrapErr());
    std::vector<Listing> listings;
    for (auto const& row : index.unwrap()) {
        listings.push_back({row["id"].asString().unwrap(), row["title"].asString().unwrapOr(""),
            row["author"].asString().unwrapOr(""), parseCategory(row["category"].asString().unwrap()).unwrap(),
            row["createdAt"].asInt().unwrapOr(0)});
    }
    return geode::Ok(std::move(listings));
}

geode::Result<Preset> ShopLibrary::read(std::string const& id) const {
    if (!validId(id)) return geode::Err("ID invalido / Invalid ID");
    auto json = readJson(m_directory / (id + ".json"), kMaxPresetBytes);
    if (!json) return geode::Err(json.unwrapErr());
    return presetFromJson(json.unwrap());
}

geode::Result<std::string> ShopLibrary::add(Preset const& preset) {
    auto valid = validatePreset(preset);
    if (!valid) return geode::Err(valid.unwrapErr());
    auto index = readIndex();
    if (!index) return geode::Err(index.unwrapErr());
    if (index.unwrap().size() >= kMaxLibraryEntries) return geode::Err("Biblioteca llena (500) / Library full (500)");
    auto id = makeId();
    auto path = m_directory / (id + ".json");
    if (!paimon::file::writeAtomically(path, presetToJson(preset).dump())) return geode::Err("No se pudo guardar el preset / Could not save preset");
    index.unwrap().push(matjson::makeObject({{"id", id}, {"title", preset.title}, {"author", preset.author},
        {"category", categoryId(preset.category)}, {"createdAt", preset.createdAt}}));
    if (!paimon::file::writeAtomically(m_directory / "index.json", index.unwrap().dump())) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return geode::Err("No se pudo guardar el indice / Could not save index");
    }
    return geode::Ok(id);
}

geode::Result<> ShopLibrary::remove(std::string const& id) {
    if (!validId(id)) return geode::Err("ID invalido / Invalid ID");
    auto index = readIndex();
    if (!index) return geode::Err(index.unwrapErr());
    auto remaining = matjson::Value::array();
    for (auto const& row : index.unwrap()) if (row["id"].asString().unwrap() != id) remaining.push(row);
    if (!paimon::file::writeAtomically(m_directory / "index.json", remaining.dump())) return geode::Err("No se pudo guardar el indice / Could not save index");
    std::error_code ec;
    std::filesystem::remove(m_directory / (id + ".json"), ec);
    return geode::Ok();
}
} // namespace paimon::shop
