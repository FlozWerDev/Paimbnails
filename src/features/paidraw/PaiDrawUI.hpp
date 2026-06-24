#pragma once

#include "PaiDrawManager.hpp"
#include "../../framework/EventBus.hpp"
#include "../../utils/PaimonDrawNode.hpp"
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

namespace paidraw {

class PaiDrawResultsLayer;
class PaiDrawGameLayer;
class PaiDrawRoomLayer;

class PaiDrawLobbyLayer : public cocos2d::CCLayer {
public:
    static PaiDrawLobbyLayer* create();
    static cocos2d::CCScene* scene();

    bool init() override;
    void keyBackClicked() override;
    void onExit() override;
    void onCreateRoom(cocos2d::CCObject*);
    void onJoinRoom(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void onBack(cocos2d::CCObject*);
    void refreshLists();

private:
    void buildLayout();
    void rebuildOnlineList();
    void updateHeader();
    cocos2d::CCNode* createPlayerRow(PlayerInfo const& player, float width, float height);

    geode::ScrollLayer* m_onlineScroll = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    paimon::SubscriptionHandle m_connectionSub = 0;
    paimon::SubscriptionHandle m_lobbySub = 0;
};

class PaiDrawRoomsLayer : public cocos2d::CCLayer {
public:
    static PaiDrawRoomsLayer* create();
    static cocos2d::CCScene* scene();

    bool init() override;
    void keyBackClicked() override;
    void onExit() override;
    void onBack(cocos2d::CCObject*);
    void onRefresh(cocos2d::CCObject*);
    void onCreateRoom(cocos2d::CCObject*);
    void onJoinRoom(cocos2d::CCObject* sender);
    void refreshLists();

private:
    void buildLayout();
    void rebuildRoomList();
    cocos2d::CCNode* createRoomRow(RoomInfo const& room, float width, float height);

    geode::ScrollLayer* m_roomScroll = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    paimon::SubscriptionHandle m_connectionSub = 0;
    paimon::SubscriptionHandle m_lobbySub = 0;
};

class PaiDrawCreateRoomLayer : public cocos2d::CCLayer {
public:
    static PaiDrawCreateRoomLayer* create(bool editMode = false);
    static cocos2d::CCScene* scene(bool editMode = false);

protected:
    bool init(bool editMode);
    void keyBackClicked() override;

private:
    void buildLayout();
    void loadInitialValues();
    void onBack(cocos2d::CCObject*);
    void onCreate(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject* sender);
    void onRounds(cocos2d::CCObject* sender);
    void onTime(cocos2d::CCObject* sender);
    void onLanguage(cocos2d::CCObject* sender);
    void onPlayersChanged(cocos2d::CCObject* sender);
    void refreshSelectionColors();

    cocos2d::CCMenu* m_menu = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    geode::TextInput* m_passwordInput = nullptr;
    Slider* m_playerSlider = nullptr;
    cocos2d::CCLabelBMFont* m_playerCountLabel = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_modeButtons;
    std::vector<CCMenuItemSpriteExtra*> m_roundButtons;
    std::vector<CCMenuItemSpriteExtra*> m_timeButtons;
    std::vector<CCMenuItemSpriteExtra*> m_languageButtons;
    bool m_editMode = false;
    GameMode m_mode = GameMode::Classic;
    int m_rounds = 6;
    int m_timeSeconds = 90;
    WordLanguage m_language = WordLanguage::Spanish;
    int m_maxPlayers = 8;
};

class PaiDrawRoomLayer : public cocos2d::CCLayer {
public:
    static PaiDrawRoomLayer* create();
    static cocos2d::CCScene* scene();

    bool init() override;
    void keyBackClicked() override;
    void onExit() override;
    void onBack(cocos2d::CCObject*);
    void onReady(cocos2d::CCObject*);
    void onStart(cocos2d::CCObject*);
    void onSendChat(cocos2d::CCObject*);
    void onOpenCreate(cocos2d::CCObject*);
    void refreshRoom();

private:
    void buildLayout();
    void rebuildPlayers();
    void rebuildChat();
    cocos2d::CCNode* createPlayerRow(PlayerInfo const& player, float width, float height);

    geode::ScrollLayer* m_playerScroll = nullptr;
    geode::ScrollLayer* m_chatScroll = nullptr;
    geode::TextInput* m_chatInput = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_roomTitle = nullptr;
    cocos2d::CCLabelBMFont* m_roomMeta = nullptr;
    paimon::SubscriptionHandle m_roomSub = 0;
    paimon::SubscriptionHandle m_chatSub = 0;
};

class PaiDrawCanvasNode : public cocos2d::CCLayer {
public:
    enum class Tool {
        Pencil,
        Eraser,
        Line,
    };

    static PaiDrawCanvasNode* create();

    bool init() override;
    void clearCanvas();
    void addStroke(StrokeSegment const& stroke);

    // Local undo/redo (not network-synced in test mode)
    void undoLast();
    void redoLast();

    // Releases the groups retained by the redo stack (detached from the scene).
    ~PaiDrawCanvasNode();

protected:
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

private:
    cocos2d::CCPoint normalizeTouch(cocos2d::CCTouch* touch);
    void commitSegment(cocos2d::CCPoint const& nextPoint);
    void beginStrokeGroup();
    void endStrokeGroup();
    void clearRedoStack();
    void rebuildPreview(cocos2d::CCPoint const& start, cocos2d::CCPoint const& current);
    void clearPreview();

    cocos2d::CCClippingNode* m_clipper = nullptr;
    cocos2d::CCNode* m_strokeLayer = nullptr;
    PaimonDrawNode* m_overlayFrame = nullptr;
    PaimonDrawNode* m_previewNode = nullptr;
    cocos2d::CCNode* m_currentGroup = nullptr;
    std::vector<cocos2d::CCNode*> m_groupStack;
    std::vector<cocos2d::CCNode*> m_redoStack;
    cocos2d::CCPoint m_lastPoint = {0.f, 0.f};
    cocos2d::CCPoint m_strokeStart = {0.f, 0.f};
    bool m_drawing = false;
    cocos2d::ccColor3B m_currentColor = {0, 0, 0};
    float m_currentSize = 6.f;
    float m_currentOpacity = 1.f;
    Tool m_tool = Tool::Pencil;
    bool m_readOnly = false;
    float m_lastSendTimestamp = 0.f;

public:
    void setDrawColor(cocos2d::ccColor3B color) { m_currentColor = color; }
    void setBrushSize(float size) { m_currentSize = std::clamp(size, 1.f, 40.f); }
    void setBrushOpacity(float opacity) { m_currentOpacity = std::clamp(opacity, 0.05f, 1.f); }
    void setTool(Tool tool);
    Tool getTool() const { return m_tool; }
    cocos2d::ccColor3B getDrawColor() const { return m_currentColor; }
    float getBrushSize() const { return m_currentSize; }
    float getBrushOpacity() const { return m_currentOpacity; }
    void setEraser(bool value) { setTool(value ? Tool::Eraser : Tool::Pencil); }
    void setReadOnly(bool value) { m_readOnly = value; }
};

class PaiDrawGameLayer : public cocos2d::CCLayer {
public:
    static PaiDrawGameLayer* create();
    static cocos2d::CCScene* scene();

    bool init() override;
    void onExit() override;
    void keyBackClicked() override;
    void onBack(cocos2d::CCObject*);
    void onSendGuess(cocos2d::CCObject*);
    void onColor(cocos2d::CCObject* sender);
    void onBrush(cocos2d::CCObject* sender);
    void onTool(cocos2d::CCObject* sender);
    void onEraser(cocos2d::CCObject* sender);
    void onClearCanvas(cocos2d::CCObject* sender);
    void onZoom(cocos2d::CCObject* sender);
    void onUndo(cocos2d::CCObject* sender);
    void onRedo(cocos2d::CCObject* sender);
    void onBrushSlider(cocos2d::CCObject* sender);
    void onOpacitySlider(cocos2d::CCObject* sender);
    void refreshState();
    // Per-frame tick (0.1s interval) that interpolates the countdown locally
    // so the timer is smooth between server snapshots.
    void tickLocalTimer(float dt);

private:
    void buildLayout();
    void rebuildScoreboard();
    void rebuildChat();
    void refreshHeader();
    void applyCanvasZoom();
    void refreshToolButtons();
    void refreshPalette();
    void updateBrushPreview();
    cocos2d::CCNode* createScoreRow(PlayerInfo const& player, float width, float height);

    PaiDrawCanvasNode* m_canvas = nullptr;
    cocos2d::CCNode* m_canvasContainer = nullptr;
    cocos2d::CCClippingNode* m_canvasClipper = nullptr;
    geode::ScrollLayer* m_scoreScroll = nullptr;
    geode::ScrollLayer* m_chatScroll = nullptr;
    geode::TextInput* m_guessInput = nullptr;
    cocos2d::CCLabelBMFont* m_header = nullptr;
    cocos2d::CCLabelBMFont* m_wordLabel = nullptr;
    cocos2d::CCLabelBMFont* m_timerLabel = nullptr;
    cocos2d::CCLabelBMFont* m_zoomLabel = nullptr;
    cocos2d::CCLabelBMFont* m_brushSizeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_opacityLabel = nullptr;
    PaimonDrawNode* m_brushPreview = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCRect m_canvasFrameRect = {0.f, 0.f, 0.f, 0.f};
    cocos2d::CCPoint m_canvasAnchor = {0.f, 0.f};
    float m_canvasBaseScale = 1.f;
    float m_canvasZoom = 1.f;
    Slider* m_brushSlider = nullptr;
    Slider* m_opacitySlider = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_colorButtons;
    std::vector<CCMenuItemSpriteExtra*> m_brushButtons;
    std::vector<CCMenuItemSpriteExtra*> m_toolButtons;
    paimon::SubscriptionHandle m_roomSub = 0;
    paimon::SubscriptionHandle m_chatSub = 0;
    paimon::SubscriptionHandle m_roundSub = 0;
    paimon::SubscriptionHandle m_strokeSub = 0;
};

class PaiDrawResultsLayer : public cocos2d::CCLayer {
public:
    static PaiDrawResultsLayer* create();
    static cocos2d::CCScene* scene();

    bool init() override;
    void keyBackClicked() override;
    void onBackLobby(cocos2d::CCObject*);
    void onPlayAgain(cocos2d::CCObject*);
    void refreshResults();

private:
    void buildLayout();
    void rebuildTable();

    geode::ScrollLayer* m_tableScroll = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;
    paimon::SubscriptionHandle m_resultsSub = 0;
};

} // namespace paidraw
