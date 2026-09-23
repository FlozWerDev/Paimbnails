#pragma once

#include "../data/FillSpec.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>

namespace geode { class ScrollLayer; }

namespace paimon::icon_maker {

class GradientEditorPopup : public geode::Popup {
public:
    using ChangedCallback = std::function<void(GradientSpec const&)>;

    static GradientEditorPopup* create(GradientSpec initial, ChangedCallback onChanged);

protected:
    bool init(GradientSpec initial, ChangedCallback onChanged);

    void rebuildBody();
    void scheduleBodyRebuild();
    void refreshPreview();
    void notifyChanged();

    GradientSpec m_spec;
    ChangedCallback m_onChanged;

    cocos2d::CCSprite* m_preview = nullptr;
    cocos2d::CCNode* m_bodyHost = nullptr;
    geode::ScrollLayer* m_body = nullptr;
    bool m_rebuildQueued = false;
};

}  // namespace paimon::icon_maker
