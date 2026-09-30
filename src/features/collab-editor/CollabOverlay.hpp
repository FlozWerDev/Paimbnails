#pragma once

#include "CollabTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <deque>
#include <unordered_map>
#include <vector>

class LevelEditorLayer;

namespace paimon::collab {

// hud strip docks above the build toolbar; hides with editor ui and playtest.
class CollabEditorOverlay : public cocos2d::CCNode {
public:
    static CollabEditorOverlay* create(LevelEditorLayer* editor);

    void onRemoteEdit(int clientId, std::string const& name, cocos2d::CCPoint worldPos, bool isDelete);
    void onChat(ChatMessage const& msg);

    void onPeerSelection(int clientId, std::string const& name, std::vector<cocos2d::CCRect> const& rects);
    void onPeerSelectionCleared(int clientId);

    void onPeerCamera(int clientId, std::string const& name, float x, float y,
                      bool visible, PeerAppearance const& appearance);
    void onPeerCameraCleared(int clientId);

    void onPeerWorkZone(int clientId, std::string const& name, float x, float y, float w, float h);
    void onPeerWorkZoneCleared(int clientId);

    void onPeerPing(int clientId, std::string const& name, float x, float y);

private:
    bool init(LevelEditorLayer* editor);
    ~CollabEditorOverlay() override;

    void refresh(float dt);
    void updatePresence(float dt);
    float overlayZoom() const;

    cocos2d::CCNode* buildToast(ChatMessage const& msg);
    void showToast(ChatMessage const& msg);
    void layoutToasts();
    void dismissToast(cocos2d::CCNode* toast);
    void onToastExpired(cocos2d::CCObject* sender);

    struct VoiceChip {
        geode::Ref<cocos2d::CCNode> root;
        cocos2d::CCLayerColor* barFill = nullptr;
        float shown = 0.f;
        float target = 0.f;
        float silent = 0.f;
        float width = 0.f;
        bool placed = false;
    };
    void buildVoiceChip(int clientId, std::string const& name, VoiceChip& chip);
    void updateVoice(float dt);
    void layoutVoiceChips();

    struct SelectionOverlay {
        geode::Ref<cocos2d::CCDrawNode> draw;
        geode::Ref<cocos2d::CCLabelBMFont> label;
    };
    void clearSelectionNode(int clientId);

    struct CameraOverlay {
        geode::Ref<cocos2d::CCNode> ghostRoot;
        geode::Ref<cocos2d::CCDrawNode> trail;
        geode::Ref<cocos2d::CCLabelBMFont> label;
        std::deque<cocos2d::CCPoint> trailPts;
        // rendered position chases the network target each frame, smoothing 5 hz presence.
        float x = 0.f;
        float y = 0.f;
        float targetX = 0.f;
        float targetY = 0.f;
        float sinceMove = 0.f;
        bool visible = false;
        bool customCursor = false;
        bool customCursorRequested = false;
    };
    void clearCameraNode(int clientId);
    void rebuildTrail(CameraOverlay& slot, int clientId);
    // peer stopped moving: eat the wake point by point instead of leaving it.
    void drainTrails(float dt);

    struct WorkZoneOverlay {
        geode::Ref<cocos2d::CCDrawNode> draw;
        geode::Ref<cocos2d::CCLabelBMFont> label;
    };
    void clearWorkZoneNode(int clientId);

    void redrawHeatmap();
    void updateStatusBanner();

    // edit flashes tracked by hand: fade action alone leaves stuck squares.
    struct EditFlash {
        geode::Ref<cocos2d::CCSprite> node;
        float age = 0.f;
    };
    void sweepFlashes(float dt);

    // docks chat/banner/voice above the build toolbar; re-runs on toolbar/resize change.
    void layoutHudBar();
    float hudRowY() const;
    // editor ui hidden (hide ui toggle, playtest) => hide our chrome too.
    void applyVisibility();

    LevelEditorLayer* m_editor = nullptr;
    cocos2d::CCLabelBMFont* m_statusBanner = nullptr;
    cocos2d::CCLayerColor* m_statusBg = nullptr;
    geode::Ref<cocos2d::CCMenu> m_controls;
    geode::Ref<cocos2d::CCNode> m_chatButton;
    geode::Ref<cocos2d::CCNode> m_pingButton;
    void onPingButton();
    geode::ListenerHandle m_uiShowListener;
    int m_lastConnState = -1;
    bool m_lastRecovering = false;
    std::string m_lastStatus;
    bool m_editorUiHidden = false;
    bool m_hudVisible = true;
    bool m_presenceVisible = true;
    float m_hudRowY = -1.f;
    cocos2d::CCSize m_hudWin{};

    std::unordered_map<int, geode::Ref<cocos2d::CCLabelBMFont>> m_tags;

    cocos2d::CCNode* m_toastLayer = nullptr;
    std::vector<geode::Ref<cocos2d::CCNode>> m_toasts;

    cocos2d::CCNode* m_voiceLayer = nullptr;
    std::unordered_map<int, VoiceChip> m_voiceChips;

    std::unordered_map<int, SelectionOverlay> m_selections;
    std::unordered_map<int, CameraOverlay> m_cameras;
    std::unordered_map<int, WorkZoneOverlay> m_workZones;
    geode::Ref<cocos2d::CCDrawNode> m_heatDraw;
    float m_heatRedrawAge = 0.f;
    float m_lastOverlayZoom = -1.f;

    std::vector<EditFlash> m_flashes;
};

} // namespace paimon::collab
