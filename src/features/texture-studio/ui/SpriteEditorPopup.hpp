#pragma once

#include "../engine/SpritePreviewRenderer.hpp"
#include "../persist/TextureProject.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace paimon::texture_studio {

class SpriteEditorPopup : public geode::Popup {
public:
    using SavedCallback = std::function<void()>;

    static SpriteEditorPopup* create(std::string slotId,
                                     ProjectSheetRef sheetRef,
                                     std::string frameName,
                                     SavedCallback onSaved);

protected:
    bool init(std::string slotId, ProjectSheetRef sheetRef,
              std::string frameName, SavedCallback onSaved);

    enum class TintMode { Global, Custom, Skip };

    void buildPreviewColumn();
    void buildControls();

    void startPixelLoad();

    void scheduleResultRender();

    void setOriginalSprite(cocos2d::CCSprite* spr);
    void setResultSprite(cocos2d::CCSprite* spr);
    void refreshModeUi();

    void onPickImage();
    void onClearImage();
    void onReset();
    void onApply();

    // Toggles the Result box between the tinted result and the untouched
    // original, so the user can A/B the recolor in the same spot/scale.
    void onToggleCompare(cocos2d::CCObject*);

private:
    std::string     m_slotId;
    ProjectSheetRef m_sheetRef;
    std::string     m_frameName;
    SavedCallback   m_onSaved;

    TextureProject  m_project;
    SpriteSetting   m_setting;
    TintMode        m_mode = TintMode::Global;
    bool            m_imageMarkedForDelete = false;

    std::shared_ptr<ImageBuffer> m_framePixels;
    // Imagen custom decodificada (si hay).
    std::shared_ptr<ImageBuffer> m_customImage;

    std::shared_ptr<std::atomic<int>> m_renderGen
        = std::make_shared<std::atomic<int>>(0);

    // UI refs
    cocos2d::CCNode*        m_originalHost = nullptr;
    cocos2d::CCNode*        m_resultHost   = nullptr;
    cocos2d::CCSprite*      m_originalSpr  = nullptr;
    cocos2d::CCSprite*      m_resultSpr    = nullptr;
    // "Original shown inside the Result box" sprite for Compare mode.
    cocos2d::CCSprite*      m_resultCompareSpr = nullptr;
    bool                    m_compareShowsOriginal = false;
    CCMenuItemSpriteExtra*  m_compareBtn   = nullptr;
    cocos2d::CCLabelBMFont* m_hintLbl      = nullptr;
    cocos2d::CCLabelBMFont* m_imageLbl     = nullptr;
    cocos2d::CCSprite*      m_swatch1      = nullptr;
    cocos2d::CCSprite*      m_swatch2      = nullptr;
    cocos2d::CCSprite*      m_swatchGlow   = nullptr;
    CCMenuItemSpriteExtra*  m_modeGlobalBtn = nullptr;
    CCMenuItemSpriteExtra*  m_modeCustomBtn = nullptr;
    CCMenuItemSpriteExtra*  m_modeSkipBtn   = nullptr;

    void openSwatchPicker(int which);  // 0=C1 1=C2 2=Glow
    void updateSwatches();
    void updateHint();
};

}  // namespace paimon::texture_studio
