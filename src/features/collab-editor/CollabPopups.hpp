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

// Display name: collab setting, GD player name, or "editor".
std::string defaultDisplayName();

// Close room popups before scene swaps to avoid stale touch priority.
void closeSessionPopups();

// Connect/create a room; setup, connecting, and connected states rebuild in place.
class CollabRoomPopup : public geode::Popup {
public:
    static CollabRoomPopup* create(GJGameLevel* hostLevel = nullptr);

private:
    enum class View { None, Setup, Connecting, Connected };

    bool init(GJGameLevel* hostLevel);
    void rebuild();
    void scheduleRebuild();
    void buildSetupView();
    cocos2d::CCNode* buildSetupPanel();
    void switchSetupTab(bool join);
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
    void onPeers(cocos2d::CCObject*);
    void refresh(float dt = 0.f);

    View m_view = View::None;
    bool m_joinTab = false;
    std::string m_createCode;
    std::string m_joinCode;

    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_setupPanel = nullptr;
    ButtonSprite* m_createTabSpr = nullptr;
    ButtonSprite* m_joinTabSpr = nullptr;
    geode::TextInput* m_codeInput = nullptr;
    cocos2d::CCLabelBMFont* m_codeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_peersLabel = nullptr;
    geode::Ref<GJGameLevel> m_hostLevel;
};

// Host-only friend invite flow.
class CollabInvitePopup : public geode::Popup, public UserListDelegate {
public:
    static CollabInvitePopup* create();
    ~CollabInvitePopup() override;

    void getUserListFinished(cocos2d::CCArray* scores, UserListType type) override;
    void getUserListFailed(UserListType type, GJErrorCode error) override;
    void userListChanged(cocos2d::CCArray*, UserListType) override {}
    void forceReloadList(UserListType) override {}

private:
    // Row snapshot used while filtering without retaining GJUserScore objects.
    struct FriendEntry {
        int accountID = 0;
        std::string name;
        std::string nameLower;
        int iconID = 1;
        int iconType = 0;
        int color1 = 0;
        int color2 = 0;
        int color3 = 0;
        bool glow = false;
    };

    bool init() override;
    void loadFriends();
    void buildList(cocos2d::CCArray* scores);
    void rebuildRows();
    void setInfo(std::string const& text);
    void onInvite(cocos2d::CCObject* sender);
    void onFocusSearch(cocos2d::CCObject* sender);

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_search = nullptr;
    cocos2d::CCLabelBMFont* m_info = nullptr;
    cocos2d::CCLabelBMFont* m_count = nullptr;
    std::vector<FriendEntry> m_friends;
    std::unordered_map<int, std::string> m_names;
};

// In-room chat and voice status.
class CollabChatPopup : public geode::Popup {
public:
    static CollabChatPopup* create();

private:
    bool init() override;
    void onSend(cocos2d::CCObject*);
    void onMic(cocos2d::CCObject*);
    void refresh(float dt = 0.f);
    void rebuildMessages();
    void tickVoice(float dt);

    geode::TextInput* m_input = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_headerLabel = nullptr;
    cocos2d::CCDrawNode* m_statusDot = nullptr;
    cocos2d::CCLabelBMFont* m_speakingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    ButtonSprite* m_micSprite = nullptr;
    cocos2d::CCLayerColor* m_micBarFill = nullptr;
    cocos2d::CCLayerColor* m_micBarTrack = nullptr;
    float m_micShown = 0.f;
    int m_connShown = -1;
    uint64_t m_lastRevision = ~0ull;
};

class HostOptionsPopup : public geode::Popup {
public:
    static HostOptionsPopup* create();

private:
    bool init() override;
    void togglePermission(bool HostPermissions::*field, bool on);

    HostPermissions m_permissions;
};

class CollabPeersPopup : public geode::Popup {
public:
    static CollabPeersPopup* create();

private:
    bool init() override;
    void rebuildList();
    void onProfile(cocos2d::CCObject* sender);
    void onKick(cocos2d::CCObject* sender);
    void refresh(float dt = 0.f);

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_info = nullptr;
    std::string m_lastPeerSignature;
};

}
