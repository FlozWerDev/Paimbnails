#pragma once
#include "HoverConfig.hpp"
#include <Geode/Geode.hpp>
#include <map>

namespace paimon::hover {
class Manager {
public:
    static Manager& get();
    Config global;
    bool linked = false, picking = false;
    std::map<std::string, Config> buttons, saved;
    void load();
    void save();
    Config resolve(std::string const& key) const;
    void set(std::string const& key, Config c);
    void group(Config c);
};
std::string buttonKey(cocos2d::CCNode* item);
void init();
void reset();
void open(std::string key = {});
void groupFromPopup();
bool popupOpen();
}
