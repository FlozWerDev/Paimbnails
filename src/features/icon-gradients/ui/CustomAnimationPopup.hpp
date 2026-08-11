#pragma once

// Editor for the "Custom" gradient animation: the user stacks up to 4
// movements, shapes each one, and watches the result on a live icon.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>

#include "../services/GradientAnimationManager.hpp"

namespace paimon::icon_gradients {

class CustomAnimationPopup : public geode::Popup {
public:
    // `onChanged` lets the parent popup resync its own controls once this one
    // closes, since both edit the same animation config.
    static CustomAnimationPopup* create(
        IconType previewType, bool secondPlayer, std::function<void()> onChanged
    );

    void onClose(cocos2d::CCObject* sender) override;

protected:
    bool init(IconType previewType, bool secondPlayer, std::function<void()> onChanged);

private:
    void buildPreview();
    void rebuildPreviewIcon();
    void rebuildStack();
    void rebuild();
    void scheduleRebuild();

    void selectLayer(size_t index);
    GradientAnimationLayer currentLayer() const;
    void writeLayer(GradientAnimationLayer const& layer);

    cocos2d::CCNode* makeToolbar(float width);
    cocos2d::CCNode* makeDescriptionRow(float width, char const* text,
                                        cocos2d::CCLabelBMFont** out);

    std::function<void()> m_onChanged;

    cocos2d::CCNode* m_previewHost = nullptr;
    cocos2d::CCNode* m_stackHost = nullptr;
    cocos2d::CCLabelBMFont* m_iconLabel = nullptr;
    cocos2d::CCLabelBMFont* m_motionDesc = nullptr;
    cocos2d::CCLabelBMFont* m_waveDesc = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;

    size_t m_selected = 0;
    size_t m_previewIndex = 0;
    bool m_secondPlayer = false;
};

} // namespace paimon::icon_gradients
