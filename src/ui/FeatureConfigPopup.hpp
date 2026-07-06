#pragma once

#include <Geode/Geode.hpp>
#include <string>

// Generic popup showing grouped settings for a Paimon Hub feature; a featureKey
// maps to a builder in the internal registry that renders the widgets.

namespace paimon::ui {

class FeatureConfigPopup : public geode::Popup {
public:
    static FeatureConfigPopup* create(std::string const& featureKey);

    static bool hasFeatureKey(std::string const& featureKey);

protected:
    bool init(std::string const& featureKey);

    geode::ScrollLayer* m_scroll = nullptr;
};

// Routes a granular setting to its dedicated popup, falling back to the settings panel.
// englishGranularName: name as it appears in getGranularSettings().
// fallbackCategoryIndex: Settings Panel category to open if there's no dedicated popup.
void openFeatureConfigFor(std::string const& englishGranularName,
                          int fallbackCategoryIndex);

} // namespace paimon::ui
