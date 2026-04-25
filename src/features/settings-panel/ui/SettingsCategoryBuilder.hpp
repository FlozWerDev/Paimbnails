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

// Flat category (kept for compatibility)
struct SettingsCategory {
    std::string id;
    std::string name;
    std::string iconFrame;
    std::function<void(cocos2d::CCNode* content, float width)> buildContent;
};

std::vector<SettingsGroup> const& getAllGroups();
std::vector<SettingsCategory> const& getAllCategories();

} // namespace paimon::settings_ui
