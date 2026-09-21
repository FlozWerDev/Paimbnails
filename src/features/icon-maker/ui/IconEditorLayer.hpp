#pragma once
// Editor del Creador de Iconos: icono vivo a la izquierda, zonas y pestanas a
// la derecha.

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

    // Entrada desde el lienzo: cambia zona y capa a la vez y deja la
    // reconstruccion del panel para el siguiente frame, porque llega desde
    // dentro del reparto de toques.
    void selectFromCanvas(std::string const& zoneKey, int pieceIndex);
    void pushCanvasSelection();

    // Envuelve una modificacion para que deshacer, autoguardado y vista previa
    // queden en su sitio. `coalesceKey` junta una rafaga de cambios (arrastrar
    // un slider) en un solo paso de deshacer.
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

    // Los tres avisos de la primera vez, sobre las zonas del editor.
    void maybeShowTour();

    void saveProject(bool notify);
    void setStatus(std::string const& text, bool good = true);

    // -- preview -------------------------------------------------------------
    // `fast` re-dibuja solo la zona activa, que es lo unico que cambia
    // mientras se arrastra en el lienzo.
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

    // Los botones de vista dicen en su texto en que estado estan.
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
