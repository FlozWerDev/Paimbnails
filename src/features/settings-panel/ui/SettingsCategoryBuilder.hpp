#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace paimon::settings_ui {

struct SettingsSubcategory {
    std::string id;
    std::string name;
    std::function<void(cocos2d::CCNode* content, float width)> buildContent;
};

struct SettingsGroup {
    std::string id;
    std::string name;
    std::vector<SettingsSubcategory> subcategories;
};

std::vector<SettingsGroup> const& getAllGroups();

} // namespace paimon::settings_ui
