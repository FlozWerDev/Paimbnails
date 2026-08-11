#pragma once
// Editor del Creador de Iconos.
//
// Layout: a la izquierda el icono entero, vivo y tocable (tocas una zona para
// saltar a ella, arrastras para mover la capa elegida); a la derecha una sola
// columna con scroll -- zonas, capas, pintura, forma -- en vez de pestanas
// escondidas. Todo lo secundario vive en popups, para que la columna nunca
// pase de "una fila = una decision".

#include "../data/IconAnatomy.hpp"
#include "../data/IconHistory.hpp"
#include "../data/IconProject.hpp"

#include <Geode/Geode.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace geode { class ScrollLayer; }

namespace paimon::icon_maker {

class EditorCanvas;

class IconEditorLayer : public cocos2d::CCLayer {
public:
    static void open(std::string const& slotId);
    static IconEditorLayer* create(std::string const& slotId);

protected:
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
    void scheduleInspectorRebuild();
    void rebuildInspector();

    cocos2d::CCNode* buildLayersCard(float width);
    cocos2d::CCNode* buildPaintCard(float width);
    cocos2d::CCNode* buildShapeCard(float width);
    cocos2d::CCNode* buildProjectCard(float width);

    // -- model ---------------------------------------------------------------
    std::vector<SlotDef> visibleZones() const;
    std::string currentSlotKey() const;
    IconSlotContent& currentSlot();
    IconPiece* selectedPiece();
    void selectDefaultPiece();

    void selectPart(int part);
    void selectZone(int zoneIndex);
    void selectZoneByKey(std::string const& storageKey);
    void selectPiece(int index);

    // Wraps a mutation so undo/redo, autosave and the preview all stay in sync.
    // `coalesceKey` merges a burst of related edits (one slider drag) into a
    // single undo step.
    void edit(std::string coalesceKey, std::function<void()> mutate);
    void applyRestoredProject();

    // -- actions -------------------------------------------------------------
    void onBack();
    void onUndo();
    void onRedo();
    void onApply();
    void onExport();
    void onRename();
    void onProjectMenu();
    void onLayerMenu(int pieceIndex);
    void onAddImportLayer();
    void onAddTemplateLayer();
    void onReplaceShape();
    void onCopyPartToOthers();
    void onCopyLayerToZones(int pieceIndex);

    void saveProject(bool notify);
    void setStatus(std::string const& text, bool good = true);

    // -- preview -------------------------------------------------------------
    void schedulePreview();
    void kickPreviewJob();
    void onCanvasDrag(float dxFraction, float dyFraction);
    void nudgeSelected(float dxFraction, float dyFraction);

    IconProject m_project;
    IconHistory m_history;

    int m_currentPart = 0;            // 0 = single-part; 1..4 robot/spider
    int m_zoneIndex = 0;
    int m_selectedPiece = -1;

    bool m_dirty = false;
    float m_autosaveCountdown = -1.f;
    float m_previewCountdown = -1.f;
    bool m_compileBusy = false;
    bool m_rebuildQueued = false;

    int m_backgroundMode = 0;         // 0 oscuro, 1 claro, 2 tablero
    bool m_isolateZone = false;
    bool m_showGuide = true;

    EditorCanvas* m_canvas = nullptr;
    cocos2d::CCSprite* m_undoGlyph = nullptr;
    cocos2d::CCSprite* m_redoGlyph = nullptr;
    cocos2d::CCNode* m_zoneChipsHost = nullptr;
    cocos2d::CCNode* m_inspectorHost = nullptr;
    geode::ScrollLayer* m_inspector = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCNode* m_partsHost = nullptr;

    // Botones de vista: su texto dice en que estado estan.
    cocos2d::CCLabelBMFont* m_bgToolLabel = nullptr;
    cocos2d::CCLabelBMFont* m_guideToolLabel = nullptr;
    cocos2d::CCLabelBMFont* m_isolateToolLabel = nullptr;
    float m_toolLabelW = 60.f;

    float m_inspectorScrollY = 0.f;
    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;

    std::shared_ptr<std::atomic<int>> m_generation =
        std::make_shared<std::atomic<int>>(0);
};

}  // namespace paimon::icon_maker
