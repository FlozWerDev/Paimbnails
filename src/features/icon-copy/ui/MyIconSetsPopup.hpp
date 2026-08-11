#pragma once
// Your own stylings: the plus saves whatever you are wearing right now, and the
// list puts any of them back on later.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <cstddef>

namespace paimon::iconcopy {

struct IconPreset;

class MyIconSetsPopup : public geode::Popup {
public:
    static MyIconSetsPopup* create();

protected:
    bool init();
    void rebuild();
    cocos2d::CCNode* makeRow(IconPreset const& preset, std::size_t index, float width);

    void onAdd(cocos2d::CCObject*);
    void use(IconPreset const& preset);
    void showIcons(IconPreset const& preset);
    void rename(IconPreset const& preset);
    void erase(IconPreset const& preset);
    void queueRebuild();

    cocos2d::CCNode* m_body = nullptr;
};

}  // namespace paimon::iconcopy
