#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../services/TransitionManager.hpp"

// ════════════════════════════════════════════════════════════
// CustomTransitionEditorPopup — editor visual completo para
// transiciones custom DSL dentro de Geometry Dash.
//
// Features:
//   - Lista scrollable de comandos con add/remove/reorder
//   - Editor por comando: accion, target, duracion, valores
//   - Soporte de imagenes overlay
//   - Preview en vivo de la transicion
//   - Guardar/cargar configuracion
// ════════════════════════════════════════════════════════════

class CustomTransitionEditorPopup : public geode::Popup {
protected:
    bool init(TransitionConfig* config, bool isGlobal);

    // ── State ──
    TransitionConfig* m_config = nullptr;
    bool m_isGlobal = true;
    std::vector<TransitionCommand> m_commands;
    int m_selectedIdx = -1;

    // ── UI — Command list ──
    geode::ScrollLayer* m_commandScroll = nullptr;
    cocos2d::CCNode* m_commandListMenu = nullptr;
    cocos2d::CCSize m_scrollSize;

    // ── UI — Command editor panel ──
    cocos2d::CCNode* m_editorPanel = nullptr;
    cocos2d::CCLabelBMFont* m_actionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_targetLabel = nullptr;
    cocos2d::CCLabelBMFont* m_durationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_fromLabel = nullptr;
    cocos2d::CCLabelBMFont* m_toLabel = nullptr;
    cocos2d::CCLabelBMFont* m_extraLabel = nullptr;
    cocos2d::CCLabelBMFont* m_delayLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;

    // ── Preview ──
    cocos2d::CCNode* m_previewArea = nullptr;
    cocos2d::CCLayerColor* m_previewFrom = nullptr;
    cocos2d::CCLayerColor* m_previewTo = nullptr;

    // ── Methods ──
    void rebuildCommandList();
    void selectCommand(int idx);
    void updateEditorPanel();
    void updatePreviewArea();
    void refreshDisplay();

    // ── Callbacks ──
    void onAddCommand(cocos2d::CCObject*);
    void onRemoveCommand(cocos2d::CCObject*);
    void onMoveUp(cocos2d::CCObject*);
    void onMoveDown(cocos2d::CCObject*);
    void onDuplicateCommand(cocos2d::CCObject*);
    void onPrevCommand(cocos2d::CCObject*);
    void onNextCommand(cocos2d::CCObject*);

    void onActionPrev(cocos2d::CCObject*);
    void onActionNext(cocos2d::CCObject*);
    void onTargetToggle(cocos2d::CCObject*);
    void onDurationDown(cocos2d::CCObject*);
    void onDurationUp(cocos2d::CCObject*);
    void onDelayDown(cocos2d::CCObject*);
    void onDelayUp(cocos2d::CCObject*);
    void onFromValDown(cocos2d::CCObject*);
    void onFromValUp(cocos2d::CCObject*);
    void onToValDown(cocos2d::CCObject*);
    void onToValUp(cocos2d::CCObject*);
    void onFromXDown(cocos2d::CCObject*);
    void onFromXUp(cocos2d::CCObject*);
    void onFromYDown(cocos2d::CCObject*);
    void onFromYUp(cocos2d::CCObject*);
    void onToXDown(cocos2d::CCObject*);
    void onToXUp(cocos2d::CCObject*);
    void onToYDown(cocos2d::CCObject*);
    void onToYUp(cocos2d::CCObject*);
    void onIntensityDown(cocos2d::CCObject*);
    void onIntensityUp(cocos2d::CCObject*);

    void onSelectImage(cocos2d::CCObject*);
    void onPreviewTransition(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onLoadPreset(cocos2d::CCObject*);
    void showPresetPicker();
    void loadPreset(int presetId);

    // ── Helpers ──
    static std::string actionDisplayName(CommandAction a);
    static std::vector<CommandAction> const& allActions();
    static int validateAndSanitizeForSave(std::vector<TransitionCommand>& commands);
    TransitionCommand& selectedCmd();

public:
    static CustomTransitionEditorPopup* create(TransitionConfig* config, bool isGlobal);
};
