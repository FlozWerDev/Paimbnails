#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>
#include "../ui/FeatureInfoPopup.hpp"

class PaimonHubLayer;

namespace paimon::hubdata {

struct HubCategoryMeta {
    std::string title;
    std::string shortDesc;
    cocos2d::ccColor3B color;
    std::function<std::vector<paimon::ui::InfoSection>()> getInfo;
};

struct HubActionMeta {
    std::string title;
    std::string sprite;
    std::function<void(PaimonHubLayer*)> onPress;
    int categoryIndex = 0;
    std::string desc;
};

struct GranularSettingMeta {
    std::string englishName;
    std::string spanishName;
    int categoryIndex;
};

std::vector<HubCategoryMeta> getHubCategories();
std::vector<HubActionMeta> getHubActions(int categoryIndex);
std::vector<GranularSettingMeta> getGranularSettings();

} // namespace paimon::hubdata
