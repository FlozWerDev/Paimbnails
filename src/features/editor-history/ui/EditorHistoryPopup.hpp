#pragma once

#include "../services/ObjectTimelineStore.hpp"
#include <Geode/Geode.hpp>

class EditorUI;

namespace paimon::editorhistory {

// History browser for Color / Groups / Layers undos.
// - Category chips (undo last of kind)
// - Scroll list of recent changes with "undo this" + focus
class EditorUndoPanel : public cocos2d::CCNode {
public:
    static EditorUndoPanel* create(EditorUI* ui);
    static EditorUndoPanel* findOpen();
    static void toggle(EditorUI* ui);
    static void closeIfOpen();

    void refresh();

protected:
    bool init(EditorUI* ui);
    void onPick(cocos2d::CCObject* sender);
    void onUndoNode(cocos2d::CCObject* sender);
    void onFocusNode(cocos2d::CCObject* sender);
    void onFilter(cocos2d::CCObject* sender);
    void onClose(cocos2d::CCObject*);
    void onTick(float);
    void rebuildList();

    EditorUI* m_ui = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_hint = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    int m_filter = -1; // -1 all, else ObjChangeKind
    uint64_t m_seen = 0;
};

} // namespace paimon::editorhistory
