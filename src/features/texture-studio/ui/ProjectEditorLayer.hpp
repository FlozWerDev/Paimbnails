#pragma once

#include "../persist/TextureProject.hpp"
#include "../data/SpriteFrameInfo.hpp"
#include "../engine/SpritePreviewRenderer.hpp"
#include "../engine/UiSpriteCatalog.hpp"

#include <Geode/Geode.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace paimon::texture_studio {

class ImageBuffer;
class ParamSliderRow;

// Full-screen pack editor: live preview column, embedded sprite browser and
// a tabbed side panel (Pack / Tune / Extra / Sprite). Per-sprite tinting and
// custom images (with scale/offset/rotation/opacity/flip) are edited inline
// in the Sprite tab — no nested popups.
class ProjectEditorLayer : public cocos2d::CCLayer {
public:
    static ProjectEditorLayer* create(std::string slotId);
    static cocos2d::CCScene* scene(std::string slotId);

    // Push the editor scene with a fade transition.
    static void open(std::string slotId);

protected:
    ~ProjectEditorLayer() override;

    bool init(std::string slotId);
    void keyBackClicked() override;
    void onBack(cocos2d::CCObject*);

    // --- layout --------------------------------------------------------
    void buildBackground();
    void buildHeader();
    void buildFooter();
    void buildPreviewPanel();
    void buildBrowserPanel();
    void buildTabsPanel();

    void buildPackTab(cocos2d::CCNode* tab, float w, float h);
    void buildTuneTab(cocos2d::CCNode* tab, float w, float h);
    void buildExtraTab(cocos2d::CCNode* tab, float w, float h);
    void buildSpriteTab(cocos2d::CCNode* tab, float w, float h);
    void selectTab(int index);

    // --- browser -------------------------------------------------------
    struct Entry {
        std::string frameName;
        int         sheetIndex = -1;
        SpriteKind  kind = SpriteKind::Other;
    };

    void buildEntries();
    void applyFilter();
    void rebuildGrid();
    void refreshFilterButtons();
    void requestThumbnails(std::vector<Entry> entries, int generation);
    void applyThumbnail(int slot, int generation,
                        std::shared_ptr<SpritePreviewResult> result);
    void selectEntry(Entry const& entry);
    void highlightSelectedCell();

    // --- preview -------------------------------------------------------
    void startSelectionPixelLoad();
    void refreshPreviewTint();
    void renderPreviewAfterDelay(float);
    void setOriginalSprite(cocos2d::CCSprite* spr);
    void setResultSprite(cocos2d::CCSprite* spr);

    // --- per-sprite editing ---------------------------------------------
    // Setting for the selected sprite (map entry, created on demand).
    SpriteSetting currentSetting() const;
    void storeSetting(SpriteSetting const& s);
    void refreshSpriteTabUi();
    void onPickImage();
    void onClearImage();
    void onResetSprite();

    // --- actions ---------------------------------------------------------
    void onSave(cocos2d::CCObject*);
    void onGenerate(cocos2d::CCObject*);
    void onAutoTune(cocos2d::CCObject*);
    void markEdited(bool affectsPreview);
    void setBusy(bool busy);
    void setStatus(std::string const& text);

    SpritePreviewOptions makePreviewOptions() const;

private:
    std::string    m_slotId;
    TextureProject m_project;

    // browser state
    std::vector<Entry> m_all;
    std::vector<int>   m_filtered;
    int  m_page = 0;
    int  m_filterMode = 0;  // 0 buttons, 1 all UI, 2 edited
    std::string m_search;
    int  m_gridCols = 3;
    int  m_gridRows = 3;
    cocos2d::CCNode* m_gridHost = nullptr;
    cocos2d::CCMenu* m_gridMenu = nullptr;
    cocos2d::CCLabelBMFont* m_pageLbl  = nullptr;
    cocos2d::CCLabelBMFont* m_countLbl = nullptr;
    CCMenuItemSpriteExtra* m_filterButtonsBtn = nullptr;
    CCMenuItemSpriteExtra* m_filterAllUiBtn   = nullptr;
    CCMenuItemSpriteExtra* m_filterEditedBtn  = nullptr;

    // selection
    bool        m_hasSelection = false;
    Entry       m_selected;
    int         m_selectedCellTag = -1;

    // preview state
    cocos2d::CCNode*   m_originalHost = nullptr;
    cocos2d::CCNode*   m_resultHost   = nullptr;
    cocos2d::CCSprite* m_originalSpr  = nullptr;
    cocos2d::CCSprite* m_resultSpr    = nullptr;
    cocos2d::CCLabelBMFont* m_coverageLbl = nullptr;
    cocos2d::CCLabelBMFont* m_previewNameLbl = nullptr;
    std::shared_ptr<ImageBuffer> m_previewPixels;
    std::shared_ptr<ImageBuffer> m_customImage;
    SpriteFrameInfo m_previewFrameInfo;

    // tabs
    std::array<cocos2d::CCNode*, 4>       m_tabs{};
    std::array<CCMenuItemSpriteExtra*, 4> m_tabBtns{};
    int m_activeTab = 0;

    // pack tab widgets
    cocos2d::CCSprite* m_packSwatch[4] = {};
    ParamSliderRow*    m_brightnessRow = nullptr;
    int m_presetIndex = 0;

    // extra tab widgets
    cocos2d::CCLabelBMFont* m_scopeBtnLbl = nullptr;

    // sprite tab widgets
    cocos2d::CCLabelBMFont* m_spriteNameLbl = nullptr;
    cocos2d::CCLabelBMFont* m_spriteHintLbl = nullptr;
    CCMenuItemSpriteExtra*  m_modeGlobalBtn = nullptr;
    CCMenuItemSpriteExtra*  m_modeCustomBtn = nullptr;
    CCMenuItemSpriteExtra*  m_modeSkipBtn   = nullptr;
    cocos2d::CCSprite*      m_spriteSwatch[4] = {};
    cocos2d::CCMenu*        m_imageRow = nullptr;
    CCMenuItemSpriteExtra*  m_fitBtn   = nullptr;
    CCMenuItemSpriteExtra*  m_imgModeBtn = nullptr;
    CCMenuItemToggler*      m_flipXTog = nullptr;
    CCMenuItemToggler*      m_flipYTog = nullptr;
    ParamSliderRow*         m_scaleRow = nullptr;
    ParamSliderRow*         m_offXRow  = nullptr;
    ParamSliderRow*         m_offYRow  = nullptr;
    ParamSliderRow*         m_rotRow   = nullptr;
    ParamSliderRow*         m_opacityRow = nullptr;
    cocos2d::CCLabelBMFont* m_imageStateLbl = nullptr;

    // footer
    cocos2d::CCLabelBMFont* m_statusLbl = nullptr;
    CCMenuItemSpriteExtra*  m_saveBtn = nullptr;
    CCMenuItemSpriteExtra*  m_genBtn  = nullptr;

    // async guards. Pixel loads and result renders use SEPARATE generations:
    // a render must never invalidate an in-flight selection pixel load.
    std::shared_ptr<std::atomic<bool>> m_closed
        = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<int>> m_previewGeneration
        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> m_renderGeneration
        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> m_thumbGeneration
        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<bool>> m_generating
        = std::make_shared<std::atomic<bool>>(false);
};

}  // namespace paimon::texture_studio
