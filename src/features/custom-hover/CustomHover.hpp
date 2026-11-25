#pragma once
#include "HoverConfig.hpp"
#include <Geode/Geode.hpp>
#include <cstdint>
#include <map>

namespace paimon::hover {

class Manager {
public:
    static Manager& get();
    Config global;
    bool linked = false;
    bool picking = false;
    std::map<std::string, Config> buttons, saved;

    void load();
    void save();
    void saveIfDirty();

    Config resolve(std::string const& key) const;
    bool hasOverride(std::string const& key) const;
    void set(std::string const& key, Config c, bool persist = true);
    void removeButton(std::string const& key);
    void setLinked(bool value);
    void group(Config c);
    bool savePreset(std::string const& name, Config c);
    void deletePreset(std::string const& name);
    void resetAll();

    uint64_t revision() const noexcept { return m_revision; }
    bool isDirty() const noexcept { return m_dirty; }

private:
    uint64_t m_revision = 1;
    bool m_dirty = false;
};

std::string buttonKey(cocos2d::CCNode* item);
void init();
void reset();
void open(std::string key = {});
void groupFromPopup();
bool popupOpen();

} // namespace paimon::hover
