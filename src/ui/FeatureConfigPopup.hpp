#pragma once
#include "PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::ui {

class FeatureConfigPopup : public PaimonPopup {
public:
    static FeatureConfigPopup* create(std::string const& featureKey);

    static bool hasFeatureKey(std::string const& featureKey);

protected:
    bool init(std::string const& featureKey);

    geode::ScrollLayer* m_scroll = nullptr;
};

// routes a granular setting to its dedicated popup, else the settings panel.
void openFeatureConfigFor(std::string const& englishGranularName,
                          int fallbackCategoryIndex);

} // namespace paimon::ui
