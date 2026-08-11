#pragma once

#include "../persist/TextureProject.hpp"
#include "../data/ImageTransform.hpp"
#include "../data/SpriteFrameInfo.hpp"
#include "../engine/FusionAsset.hpp"
#include "../engine/FusionEngine.hpp"
#include "../engine/MaskBuilder.hpp"
#include "../engine/SpritePreviewRenderer.hpp"
#include "../engine/UiSpriteCatalog.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace paimon::texture_studio {

class ImageBuffer;
class ParamSliderRow;

// Dedicated full-screen Fusion layer (own scene — not a tab inside the pack
// editor). Layout: asset browser (left) | Original + Result | tools (right).
// The mask can be painted before a texture is picked; the painted region is
// shown as a green highlight until a texture is loaded. Paint-bucket fill on
// ORIGINAL colours; texture/GIF is never recolored by pack tint. Arrow keys /
// drag move the stamp in whole pixels.
class FusionEditorLayer : public cocos2d::CCLayer {
public:
    static FusionEditorLayer* create(std::string slotId, std::string frameName = {});
    static cocos2d::CCScene* scene(std::string slotId, std::string frameName = {});
    static void open(std::string slotId, std::string frameName = {});

protected:
    ~FusionEditorLayer() override;

    bool init(std::string slotId, std::string frameName);
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;
    void onBack(cocos2d::CCObject*);
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void buildBackground();
    void buildHeader();
    void buildBrowser();
    void buildPreviews();
    void buildTools();
    void refreshToolsUi();
    void refreshTextureThumb();
    void setStatus(std::string const& text);

    struct Entry {
        std::string frameName;
        int sheetIndex = -1;
        SpriteKind kind = SpriteKind::Other;
    };
    void buildEntries();
    void applyBrowserFilter();
    void rebuildBrowser();
    void requestThumbnails(std::vector<Entry> entries, int generation);
    void applyThumbnail(int slot, int generation,
                        std::shared_ptr<ImageBuffer> image);
    void highlightBrowserSelection();
    void refreshBrowserBadges();

    void selectFrame(std::string const& frameName);
    void loadPixelsForSelection();
    void loadFusionForSelection();
    void unloadFusion();
    void refreshPreview();
    void renderPreview(float);
    // Instant main-thread preview while dragging (uses stamp/mask caches).
    void invalidateStampCache();
    void ensureStampCache();
    void renderPreviewFast();
    void setOriginalSprite(cocos2d::CCSprite* spr);
    void setResultSprite(cocos2d::CCSprite* spr);

    SpriteSetting currentSetting() const;
    void storeSetting(SpriteSetting const& s);
    FusionApplyOptions makeFusionOptions(SpriteSetting const& s) const;

    void onPickTexture();
    void onClearFusion();
    void onCycleBlend();
    void onCycleFit();
    void onNudge(int dx, int dy);
    void onResetPlacement();
    void onUndo();
    void pushUndo();
    void clearUndo();
    void persistMask();

    bool mapTouchToSpriteFloatOn(cocos2d::CCSprite* sprite,
                                 cocos2d::CCTouch* touch,
                                 float& outX, float& outY,
                                 bool requireInside);
    void applyFill(int px, int py);
    void endGesture(cocos2d::CCTouch* touch, bool cancelled = false);
    void startAnim();
    void stopAnim();
    void tickAnim(float dt);

private:
    std::string m_slotId;
    TextureProject m_project;
    std::vector<Entry> m_entries;
    std::vector<int> m_filtered;
    std::string m_search;
    int m_page = 0;
    int m_browserCols = 2;
    int m_browserRows = 4;
    bool m_hasSelection = false;
    Entry m_selected;

    cocos2d::CCNode* m_browserHost = nullptr;
    cocos2d::CCMenu* m_browserMenu = nullptr;
    cocos2d::CCLabelBMFont* m_pageLbl = nullptr;

    cocos2d::CCNode* m_originalHost = nullptr;
    cocos2d::CCNode* m_resultHost = nullptr;
    cocos2d::CCSprite* m_originalSpr = nullptr;
    cocos2d::CCSprite* m_resultSpr = nullptr;
    cocos2d::CCLabelBMFont* m_statusLbl = nullptr;
    cocos2d::CCLabelBMFont* m_frameLbl = nullptr;
    cocos2d::CCNode* m_texThumbHost = nullptr;

    std::shared_ptr<ImageBuffer> m_pixels;
    SpriteFrameInfo m_frameInfo;

    std::shared_ptr<FusionAsset> m_asset;
    std::shared_ptr<MaskBuffer> m_mask;
    static constexpr int kUndoMax = 24;
    std::vector<std::shared_ptr<MaskBuffer>> m_undo;
    std::size_t m_frameIndex = 0;
    float m_frameAccum = 0.f;
    bool m_animating = false;
    bool m_paintArmed = true;

    bool m_touchActive = false;
    bool m_dragging = false;
    bool m_touchOnResult = false;
    float m_touchStartX = 0.f, m_touchStartY = 0.f;
    int m_dragStartPx = 0, m_dragStartPy = 0;

    // Live-drag caches: rebuilt when mask/texture/transform/placement changes.
    std::vector<float> m_cachedCoverage;
    ImageBuffer m_cachedStamp;
    bool m_cacheValid = false;
    int m_cacheFrame = -1;
    int m_cacheW = 0, m_cacheH = 0;
    int m_cachePixelX = 0, m_cachePixelY = 0;
    ImageTransform m_cacheTransform{};
    float m_cacheOpacity = -1.f;
    FusionBlendMode m_cacheBlend = FusionBlendMode::Replace;
    // Throttle fast preview to ~60 Hz without stacking work.
    bool m_fastPreviewPending = false;

    CCMenuItemSpriteExtra* m_blendBtn = nullptr;
    CCMenuItemSpriteExtra* m_fitBtn = nullptr;
    CCMenuItemSpriteExtra* m_undoBtn = nullptr;
    CCMenuItemToggler* m_paintTog = nullptr;
    CCMenuItemToggler* m_flipXTog = nullptr;
    CCMenuItemToggler* m_flipYTog = nullptr;
    cocos2d::CCLabelBMFont* m_stateLbl = nullptr;
    cocos2d::CCLabelBMFont* m_pixelLbl = nullptr;
    cocos2d::CCLabelBMFont* m_hintLbl = nullptr;
    ParamSliderRow* m_scaleRow = nullptr;
    ParamSliderRow* m_offXRow = nullptr;
    ParamSliderRow* m_offYRow = nullptr;
    ParamSliderRow* m_rotRow = nullptr;
    ParamSliderRow* m_opacRow = nullptr;
    ParamSliderRow* m_tolRow = nullptr;   // colour radius
    ParamSliderRow* m_expandRow = nullptr; // spatial expand radius

    std::shared_ptr<std::atomic<bool>> m_closed
        = std::make_shared<std::atomic<bool>>(false);
    std::shared_ptr<std::atomic<int>> m_loadGen
        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> m_renderGen
        = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> m_thumbGen
        = std::make_shared<std::atomic<int>>(0);
};

}  // namespace paimon::texture_studio
