#pragma once

#include "../data/ButtonSkin.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::buttonskin {

matjson::Value skinToJson(ButtonSkin const& skin);
ButtonSkin skinFromJson(matjson::Value const& value);
// changes whenever any saved field changes; tells the runtime to repaint.
size_t skinHash(ButtonSkin const& skin);

// saved under "button-skins"; kept in memory because the runtime reads it every scan.
class ButtonSkinStore {
public:
    static ButtonSkinStore& get();

    std::vector<ButtonSkin> const& all();
    std::optional<ButtonSkin> find(std::string const& id);
    // skinHash of the saved skin, 0 when the id is unknown; cached per save.
    size_t hashOf(std::string const& id);
    bool save(ButtonSkin skin);
    bool remove(std::string const& id);
    std::string makeUniqueId(std::string const& name);

    // bumps on every save/remove.
    uint64_t revision() const { return m_revision; }

    static std::filesystem::path imagesDir();

private:
    ButtonSkinStore() = default;
    void ensureLoaded();
    void write();
    void rehash();

    std::vector<ButtonSkin> m_skins;
    std::unordered_map<std::string, size_t> m_hashes;
    bool m_loaded = false;
    uint64_t m_revision = 1;
};

} // namespace paimon::buttonskin
