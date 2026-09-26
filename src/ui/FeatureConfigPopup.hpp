#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::ui {

class FeatureConfigPopup : public geode::Popup {
public:
    static FeatureConfigPopup* create(std::string const& featureKey);

    static bool hasFeatureKey(std::string const& featureKey);

protected:
    bool init(std::string const& featureKey);

    geode::ScrollLayer* m_scroll = nullptr;
};

// Routes a granular setting to its dedicated popup, else the settings panel.
void openFeatureConfigFor(std::string const& englishGranularName,
                          int fallbackCategoryIndex);

} // namespace paimon::ui
