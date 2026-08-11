#pragma once

// Lets the user tell the feed directly which Level Tags to chase and which to
// never show again. A pinned tag outranks anything the model inferred from
// play history, so this is the strongest lever the user has over the feed.

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <string>
#include <vector>

namespace paimon::foryou {

class TagPreferencesPopup : public geode::Popup {
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
        // A real GD ButtonSprite, so the three states recolour the same way the
        // game tints its own buttons.
        ButtonSprite* pill = nullptr;
    };

    void refreshChip(Chip const& chip) const;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_placeholder = nullptr;
    std::vector<Chip> m_chips;
};

} // namespace paimon::foryou
