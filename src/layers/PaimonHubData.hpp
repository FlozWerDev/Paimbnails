#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>
#include "../ui/FeatureInfoPopup.hpp"

class PaimonHubLayer;

// Shared metadata between the two Hub skins (original + GD style).
// Defined in PaimonHubLayer.cpp; consumed by PaimonHubLayerGD.cpp too.
namespace paimon::hubdata {

struct HubCategoryMeta {
    std::string title;
    std::string shortDesc;
    cocos2d::ccColor3B color;
    // returns info sections for FeatureInfoPopup
    std::function<std::vector<paimon::ui::InfoSection>()> getInfo;
};

struct HubActionMeta {
    std::string title;
    std::string sprite;
    std::function<void(PaimonHubLayer*)> onPress;
    int categoryIndex = 0;
    // one-line hint shown by the GD skin under the action title
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
