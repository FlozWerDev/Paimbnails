#pragma once

#include "CollabTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/UserListDelegate.hpp>
#include <unordered_map>

class GJGameLevel;

namespace geode {
class TextInput;
}

class GJUserScore;

namespace geode {
class ScrollLayer;
}

namespace paimon::collab {

// Returns true if the user has already entered the access code this install.
bool isUnlocked();

// Display name shown to peers: the collab-username setting if set, else the GD
// player name, else "editor". Shared by the room/invite/presence flows.
std::string defaultDisplayName();

// Closes any open collab gate/room popup on the running scene. Called before
// the manager pushes an editor scene (and after popping back), because a popup
// that survives a scene swap keeps broken touch priority and looks frozen.
void closeSessionPopups();

// Asks for the access code ("paimon5"). On success, unlocks and opens the room
// popup.
class CollabGatePopup : public geode::Popup {
public:
    static CollabGatePopup* create();

private:
    bool init();
    void onConfirm(cocos2d::CCObject*);

    geode::TextInput* m_codeInput = nullptr;
};

// Connect / create a room on the Render server. `hostLevel` is the level to
// host if we end up creating the room (passed straight to the manager).
//
// The popup is a small state machine rebuilt in place:
//  - Setup:      two tabs, "Crear sala" (shows a generated code to share) and
//                "Unirse" (paste the host's code), plus the display name.
//  - Connecting: spinner + live status + cancel.
//  - Connected:  room code + copy, who's in the room, host tools and leave.
class CollabRoomPopup : public geode::Popup {
public:
    static CollabRoomPopup* create(GJGameLevel* hostLevel = nullptr);

private:
    enum class View { None, Setup, Connecting, Connected };

    bool init(GJGameLevel* hostLevel);
    void rebuild();
    void scheduleRebuild(); // deferred a frame: never destroys the pressed button mid-callback
    void buildSetupView();
    void buildConnectingView();
    void buildConnectedView();
    void captureInputs();

    void onTabCreate(cocos2d::CCObject*);
    void onTabJoin(cocos2d::CCObject*);
    void onGenerate(cocos2d::CCObject*);
    void onCopy(cocos2d::CCObject*);
    void onPaste(cocos2d::CCObject*);
    void onAction(cocos2d::CCObject*);
    void onCancel(cocos2d::CCObject*);
    void onLeave(cocos2d::CCObject*);
    void onHostOptions(cocos2d::CCObject*);
    void onInvite(cocos2d::CCObject*);
    void refresh(float dt = 0.f);

    View m_view = View::None;
    bool m_joinTab = false;
    // Inputs survive tab switches and rebuilds through these.
    std::string m_createCode;
    std::string m_joinCode;
    std::string m_name;

    cocos2d::CCNode* m_content = nullptr;
    geode::TextInput* m_codeInput = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    cocos2d::CCLabelBMFont* m_codeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_peersLabel = nullptr;
    geode::Ref<GJGameLevel> m_hostLevel;
};

// Host-only: pick friends to invite to the current room. Loads the GD friend
// list and sends an invite that reaches online friends as a notification.
class CollabInvitePopup : public geode::Popup, public UserListDelegate {
public:
    static CollabInvitePopup* create();
    ~CollabInvitePopup() override;

    void getUserListFinished(cocos2d::CCArray* scores, UserListType type) override;
    void getUserListFailed(UserListType type, GJErrorCode error) override;
    void userListChanged(cocos2d::CCArray*, UserListType) override {}
    void forceReloadList(UserListType) override {}

private:
    bool init();
    void loadFriends();
    void buildList(cocos2d::CCArray* scores);
    void setInfo(std::string const& text);
    void onInvite(cocos2d::CCObject* sender);

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_info = nullptr;
    std::unordered_map<int, std::string> m_names; // accountID -> username
};

// Incoming invite prompt (shown by the presence client). Accept joins the room.
class CollabInvitePromptPopup : public geode::Popup {
public:
    static CollabInvitePromptPopup* create(std::string const& room, std::string const& fromName);

private:
    bool init(std::string const& room, std::string const& fromName);
    void onAccept(cocos2d::CCObject*);
    void onReject(cocos2d::CCObject*);

    std::string m_room;
};

// In-room text chat: scrollable history, room status header, "who's talking"
// line and a live mic level bar. Opened from the editor overlay.
class CollabChatPopup : public geode::Popup {
public:
    static CollabChatPopup* create();

private:
    bool init();
    void onSend(cocos2d::CCObject*);
    void onMic(cocos2d::CCObject*);
    void refresh(float dt = 0.f);
    void rebuildMessages();
    void tickVoice(float dt); // per-frame: smooth mic level bar

    geode::TextInput* m_input = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_headerLabel = nullptr;
    cocos2d::CCDrawNode* m_statusDot = nullptr;
    cocos2d::CCLabelBMFont* m_speakingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    ButtonSprite* m_micSprite = nullptr;
    cocos2d::CCLayerColor* m_micBarFill = nullptr;
    cocos2d::CCLayerColor* m_micBarTrack = nullptr;
    float m_micShown = 0.f;      // smoothed local level
    int m_connShown = -1;        // last drawn connection state (-1 = never)
    uint64_t m_lastRevision = ~0ull;
};

// Host-only permissions for non-host editors.
class HostOptionsPopup : public geode::Popup {
public:
    static HostOptionsPopup* create();

private:
    bool init();
    void onToggle(cocos2d::CCObject* sender);
    void refresh();

    HostPermissions m_permissions;
    ButtonSprite* m_songSpr = nullptr;
    ButtonSprite* m_optionsSpr = nullptr;
    ButtonSprite* m_settingsSpr = nullptr;
};

} // namespace paimon::collab
