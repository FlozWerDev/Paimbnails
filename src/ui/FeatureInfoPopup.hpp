#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// FeatureInfoPopup: scrollable popup explaining a mod feature.
// Each section has a colored title and a descriptive body; used from the Paimon Hub.

namespace paimon::ui {

struct InfoSection {
    std::string title;       // section title (e.g. "Thumbnail Size")
    std::string body;        // what it does and where it applies
    cocos2d::ccColor3B color = {100, 220, 255}; // title color
};

class FeatureInfoPopup : public geode::Popup {
public:
    static FeatureInfoPopup* create(
        std::string const& mainTitle,
        std::vector<InfoSection> const& sections
    );

protected:
    bool init(
        std::string const& mainTitle,
        std::vector<InfoSection> const& sections
    );

    void buildContent(
        std::string const& mainTitle,
        std::vector<InfoSection> const& sections
    );

    geode::ScrollLayer* m_scroll = nullptr;
};

} // namespace paimon::ui
