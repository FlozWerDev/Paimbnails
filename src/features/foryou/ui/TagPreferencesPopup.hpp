#pragma once
#include "../../../ui/PaimonPopup.hpp"

// manual tag pins outrank anything inferred from play history.
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <string>
#include <vector>

namespace paimon::foryou {

class TagPreferencesPopup : public PaimonPopup {
public:
    static TagPreferencesPopup* create();

protected:
    bool init();

    void buildContent();
    void onTagToggle(cocos2d::CCObject* sender);
    void onInstallLevelTags(cocos2d::CCObject* sender);

    struct Chip {
        std::string tag;
        cocos2d::ccColor3B color{255, 255, 255};
        // a real gd buttonsprite, so the three states recolour the same way the
        // game tints its own buttons.
        ButtonSprite* pill = nullptr;
    };

    void refreshChip(Chip const& chip) const;

    geode::ScrollLayer* m_scroll = nullptr;
    std::vector<Chip> m_chips;
};

} // namespace paimon::foryou
