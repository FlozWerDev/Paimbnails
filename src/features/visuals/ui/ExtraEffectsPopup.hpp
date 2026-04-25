#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/Slider.hpp>
#include <sstream>

class ExtraEffectsPopup : public geode::Popup {
protected:
    cocos2d::CCNode* m_rowContainer = nullptr;
    cocos2d::CCMenu* m_rowMenu = nullptr;
    std::vector<cocos2d::CCLabelBMFont*> m_labels;
    std::vector<int> m_indices;
    static constexpr int MAX_EXTRA = 4;

    std::vector<std::string> m_styles;
    geode::CopyableFunction<void()> m_onChanged;

    bool init();

    void rebuildRows();
    void save();

    std::string displayName(std::string const& s);

public:
    static ExtraEffectsPopup* create();
    void setOnChanged(geode::CopyableFunction<void()> cb) { m_onChanged = std::move(cb); }
};
