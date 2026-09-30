#pragma once
// icon maker editor: live icon left, zones and tabs right.

#include "IconMakerUI.hpp"
#include "../data/IconAnatomy.hpp"
#include "../data/IconHistory.hpp"
#include "../data/IconProject.hpp"
#include "../engine/PieceRenderer.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace geode { class ScrollLayer; }

namespace paimon::icon_maker {

class EditorCanvas;
struct IconTheme;

class IconEditorLayer : public cocos2d::CCLayer {
public:
    static void open(std::string const& slotId);
    static IconEditorLayer* create(std::string const& slotId);

protected:
    enum class Tab : int { Layers = 0, Paint = 1, Shape = 2, Icon = 3 };

    bool init(std::string const& slotId);
    void keyBackClicked() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;

    // -- build ---------------------------------------------------------------
    void buildBackground();
    void buildTopBar();
    void buildWorkspace();
    void buildInspector();

    void refreshTopBar();
    void refreshViewTools();
    void refreshZoneChips();
    void refreshSelectionStrip();
    void scheduleInspectorRebuild();
    void rebuildInspector();

    std::vector<cocos2d::CCNode*> buildLayersTab(float width);
    std::vector<cocos2d::CCNode*> buildPaintTab(float width);
    std::vector<cocos2d::CCNode*> buildShapeTab(float width);
    std::vector<cocos2d::CCNode*> buildIconTab(float width);

    // -- model ---------------------------------------------------------------
    std::vector<SlotDef> visibleZones() const;
    std::string currentSlotKey() const;
    IconSlotContent& currentSlot();
    IconPiece* selectedPiece();
    void selectDefaultPiece();

    void selectPart(int part);
    void selectZone(int zoneIndex);
    void selectPiece(int index);
    void selectTab(Tab tab);

    // canvas entry: swaps zone+layer at once, panel rebuild deferred a frame
    // (arrives inside touch dispatch).
    void selectFromCanvas(std::string const& zoneKey, int pieceIndex);
    void pushCanvasSelection();

    // wraps a mutation so undo, autosave and preview stay consistent.
    // `coalescekey` merges a burst (slider drag) into one undo step.
    void edit(std::string coalesceKey, std::function<void()> mutate);
    void applyRestoredProject();

    // -- actions -------------------------------------------------------------
    void onBack();
    void onUndo();
    void onRedo();
    void onApply();
    void onExport();
    void onTry();
    void onRename();
    void onProjectMenu();
    void onLayerMenu(int pieceIndex);
    void onAddImportLayer();
    void onAddTemplateLayer();
    void onLoadWholeIcon();
    void onReplaceShape();
    void adoptShapeFromProject(std::string const& projectId);
    void onCopyPartToOthers();
    void onCopyLayerToZones(int pieceIndex);
    void onSaveStyle();
    void onApplyStyle();
    void onThemeMenu();
    void applyTheme(IconTheme const& theme, bool wholeIcon);
    void applyFillToZone(FillSpec const& fill, std::string const& storageKey);
    void alignSelected(ui::AlignMode mode);
    void pickColor(cocos2d::ccColor3B color);

    // first-run hints over the editor zones.
    void maybeShowTour();

    void saveProject(bool notify);
    void setStatus(std::string const& text, bool good = true);

    // -- preview ------------------------------------------------------------
    // `fast` repaints the active zone only (all that changes mid-drag).
    void schedulePreview(bool fast);
    void kickPreviewJob();
    void applyPreview(std::vector<std::pair<std::string, SlotRender>> rendered,
                      std::string const& activeKey);
    std::vector<std::string> drawOrderKeys() const;

    IconProject m_project;
    IconHistory m_history;

    int m_currentPart = 0;            // 0 = una sola parte; 1..4 robot/spider
    int m_zoneIndex = 0;
    int m_selectedPiece = -1;
    Tab m_tab = Tab::Layers;

    bool m_dirty = false;
    float m_autosaveCountdown = -1.f;
    float m_previewCountdown = -1.f;
    bool m_compileBusy = false;
    bool m_rebuildQueued = false;
    bool m_previewFast = false;
    bool m_gestureActive = false;

    int m_backgroundMode = 0;         // 0 oscuro, 1 claro, 2 tablero
    bool m_isolateZone = false;
    bool m_showGuide = true;
    bool m_eyedropper = false;

    EditorCanvas* m_canvas = nullptr;
    cocos2d::CCSprite* m_undoGlyph = nullptr;
    cocos2d::CCSprite* m_redoGlyph = nullptr;
    cocos2d::CCNode* m_zoneChipsHost = nullptr;
    cocos2d::CCNode* m_tabsHost = nullptr;
    cocos2d::CCNode* m_stripHost = nullptr;
    cocos2d::CCNode* m_inspectorHost = nullptr;
    geode::ScrollLayer* m_inspector = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCNode* m_partsHost = nullptr;

    // view buttons carry their state in their text.
    cocos2d::CCLabelBMFont* m_bgToolLabel = nullptr;
    cocos2d::CCLabelBMFont* m_guideToolLabel = nullptr;
    cocos2d::CCLabelBMFont* m_isolateToolLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pickToolLabel = nullptr;
    float m_toolLabelW = 60.f;

    std::map<std::string, SlotRender> m_slotRenders;
    std::map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_zoneTextures;
    std::map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_pieceThumbs;

    float m_inspectorScrollY = 0.f;
    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;

    std::shared_ptr<std::atomic<int>> m_generation =
        std::make_shared<std::atomic<int>>(0);
};

}  // namespace paimon::icon_maker
