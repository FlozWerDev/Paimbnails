#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../services/FeedbackService.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <vector>

class PaimonDrawNode;

namespace paimon::feedback {

// one drawn stroke; points live in screenshot pixel space.
struct FeedbackStroke {
    std::vector<cocos2d::CCPoint> points;
    cocos2d::ccColor4F color;
    float radius = 6.f;
};

class FeedbackPopup : public PaimonPopup {
public:
    // captures the live screen and opens the panel on top of it.
    static void open();
    static FeedbackPopup* getActive();

protected:
    enum class Tool { Pencil, Eraser };

    FeedbackShot m_shot;

    cocos2d::CCSprite* m_shotSprite = nullptr;
    PaimonDrawNode* m_drawNode = nullptr;
    PaimonDrawNode* m_brushPreview = nullptr;
    cocos2d::CCPoint m_shotOrigin;          // bottom-left of the fitted capture
    float m_shotScale = 1.f;

    geode::TextInput* m_textInput = nullptr;

    CCMenuItemSpriteExtra* m_pencilBtn = nullptr;
    CCMenuItemSpriteExtra* m_eraserBtn = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_sizeBtns;
    CCMenuItemSpriteExtra* m_undoBtn = nullptr;
    CCMenuItemSpriteExtra* m_redoBtn = nullptr;
    CCMenuItemSpriteExtra* m_clearBtn = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_colorBtns;
    std::vector<cocos2d::CCSprite*> m_colorRings;
    std::vector<cocos2d::CCSprite*> m_colorChips;

    std::vector<FeedbackStroke> m_strokes;
    std::vector<std::vector<FeedbackStroke>> m_undoStack;
    std::vector<std::vector<FeedbackStroke>> m_redoStack;
    std::vector<FeedbackStroke> m_touchSnapshot;

    Tool m_tool = Tool::Pencil;
    bool m_drawing = false;
    bool m_submitting = false;
    bool m_touchChanged = false;
    int m_colorIndex = 0;
    int m_sizeIndex = 1;
    cocos2d::CCPoint m_lastStrokePoint;
    cocos2d::CCPoint m_lastErasePoint;
    cocos2d::CCTouch* m_activeTouch = nullptr;

    static FeedbackPopup* s_active;

    bool init(FeedbackShot shot);
    void buildViewer();
    void buildToolbar();
    void buildTextInput();
    void buildButtons();

    float panelWidth() const;
    float inputRowY() const;
    float toolbarRowY() const;
    float viewerRowY() const;
    float viewerRowHeight() const;
    float brushScale() const;

    void drawStrokes(PaimonDrawNode* node) const;
    void redrawAnnotations();

    void setTool(Tool tool);
    void updateToolVisuals();
    void updateColorVisuals();
    void updateSizeVisuals();
    void updateHistoryVisuals();
    void updateHint();
    void updateBrushPreview();

    void beginTouchHistory();
    void commitTouchHistory();
    void pushUndo(std::vector<FeedbackStroke> snapshot);
    void undo();
    void redo();
    void clearStrokes();

    bool eraseAt(cocos2d::CCPoint const& center, float radius);
    bool eraseAlong(cocos2d::CCPoint const& from, cocos2d::CCPoint const& to, float radius);

    void onSubmit(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);

    bool touchOnViewer(cocos2d::CCTouch* touch, cocos2d::CCPoint& outPixel,
        bool clampInside = false) const;
    cocos2d::CCPoint clampToViewer(cocos2d::CCPoint const& pixel) const;

    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void onClose(cocos2d::CCObject* sender) override;

    std::vector<uint8_t> renderAnnotatedPng() const;

public:
    ~FeedbackPopup() override;
};

} // namespace paimon::feedback
