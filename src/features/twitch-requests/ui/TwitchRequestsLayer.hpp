#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include "../RequestQueue.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace geode { class ScrollLayer; }

namespace paimon::twitch {

struct LevelRequest;
namespace widgets { class SegmentBar; }

class TwitchRequestsLayer : public cocos2d::CCLayer {
public:
    static TwitchRequestsLayer* create();
    static void open();

protected:
    ~TwitchRequestsLayer() override;

    bool init() override;
    void onEnterTransitionDidFinish() override;
    void keyBackClicked() override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;
    void update(float dt) override;
    bool scrollWheel(float x, float y) override;

private:
    void buildBackground();
    void buildHeader();
    void buildSidePanel();
    void buildQueuePanel();
    void buildFooter();
    // grouped control cards in the left column.
    void buildStatsCard(cocos2d::CCNode* panel, float width);
    void refreshStats();
    void refreshTabs();
    void refreshNowPlaying();
    void refreshQueueHeader();

    // deferred entry until the transition ends, or the animation plays out
    // behind the fade.
    void enterBy(cocos2d::CCNode* node, cocos2d::CCPoint offset, float delay, bool bounce = false);
    void runIntro();

    void tick(float dt);
    void refreshStatus();
    void refreshSideButtons();
    void refreshPercents();
    void rebuildRows();
    void scheduleRebuild();
    cocos2d::CCNode* buildRow(
        size_t index, size_t position, LevelRequest const& request, float width, bool isNext);
    void buildEmptyState(bool searching, bool allFiltered, bool emptyView);

    bool inputsDiffer() const;
    void applyInputs();
    void onPrimary();
    void onSelectPlatform(int index);
    void applyPlatformSkin(bool animate = false);
    void slideIn(cocos2d::CCNode* node, float dx, float delay);
    void onToggleQueue();
    void onToggleOrder();
    // page mode: toggle and share your url.
    void onToggleWebPage();
    void onCopyWebUrl();
    void onOpenWebUrl();
    void onVerifyWebAccount();
    void onFilters();
    void onNotify();
    void onStreamOverlay();
    void onSources();
    void onQueueSelection();
    void onPlayNext();
    void playRequest(int64_t entryID);
    void openMessage(int64_t entryID);
    void openVideo(std::string url);
    void onQueueTools();
    void onSelectView(int index);
    void onSettings();
    void onBack();
    // per-row utilities.
    void copyLevelId(int levelID);
    void toggleReviewed(int64_t entryID);
    void moveRequestToFront(int64_t entryID);
    void removeRequest(int64_t entryID);
    // a removed request can be brought back for a few seconds.
    void showUndo(LevelRequest request, size_t index, std::string name);
    void onUndo();
    void hideUndo(float dt = 0.f);
    void onSweepReviewed();
    // queue search by name / id / requester.
    void onSearchChanged();
    void onClearSearch();

    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCNode* m_statusDot = nullptr;
    cocos2d::CCLabelBMFont* m_queueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_channelCaption = nullptr;
    cocos2d::CCLabelBMFont* m_commandCaption = nullptr;
    // page mode only: the address and its buttons take the channel and
    // command fields' place.
    cocos2d::CCLabelBMFont* m_webUrlLabel = nullptr;
    cocos2d::CCNode* m_webUrlPlate = nullptr;
    cocos2d::CCMenu* m_webMenu = nullptr;
    cocos2d::CCMenu* m_webVerificationMenu = nullptr;
    cocos2d::CCSprite* m_background = nullptr;
    cocos2d::CCSprite* m_titleIcon = nullptr;
    geode::TextInput* m_channelInput = nullptr;
    geode::TextInput* m_commandInput = nullptr;
    CCMenuItemSpriteExtra* m_primaryBtn = nullptr;
    CCMenuItemSpriteExtra* m_queueBtn = nullptr;
    CCMenuItemSpriteExtra* m_orderBtn = nullptr;
    CCMenuItemSpriteExtra* m_queueSelectBtn = nullptr;
    CCMenuItemSpriteExtra* m_sweepBtn = nullptr;
    CCMenuItemSpriteExtra* m_clearSearchBtn = nullptr;
    widgets::SegmentBar* m_platformTabs = nullptr;
    widgets::SegmentBar* m_viewTabs = nullptr;
    RequestView m_requestView = RequestView::All;
    cocos2d::CCNode* m_rowsHost = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;

    // live metric counters inside the summary stats panel.
    cocos2d::CCLabelBMFont* m_statReceived = nullptr;
    cocos2d::CCLabelBMFont* m_statPlayed = nullptr;
    cocos2d::CCLabelBMFont* m_statSkipped = nullptr;
    cocos2d::CCLabelBMFont* m_statWait = nullptr;
    // now-playing chip in the queue footer strip.
    cocos2d::CCNode* m_nowPlaying = nullptr;
    cocos2d::CCLabelBMFont* m_nowPlayingLabel = nullptr;
    int m_nowPlayingLevel = 0;
    cocos2d::CCNode* m_queuePanel = nullptr;

    geode::TextInput* m_searchInput = nullptr;
    std::string m_searchQuery;
    bool m_clearShown = false;
    // resting spots of the controls that slide in when the platform changes.
    std::vector<std::pair<cocos2d::CCNode*, cocos2d::CCPoint>> m_restSlots;

    struct UndoEntry {
        LevelRequest request;
        size_t index = 0;
    };
    std::optional<UndoEntry> m_undo;
    cocos2d::CCNode* m_undoToast = nullptr;
    cocos2d::CCLabelBMFont* m_undoLabel = nullptr;

    float m_listWidth = 0.f;
    float m_listHeight = 0.f;
    std::string m_lastStatus;
    std::string m_lastQueueText;
    std::string m_lastPrimaryLabel;
    std::array<std::string, 3> m_lastViewLabels;
    std::optional<size_t> m_lastReviewed;
    uint64_t m_lastQueueRevision = UINT64_MAX;
    uint64_t m_lastBriefRevision = UINT64_MAX;

    struct IntroStep {
        geode::Ref<cocos2d::CCNode> node;
        cocos2d::CCPoint target;
        float delay;
        bool bounce;
    };
    std::vector<IntroStep> m_intro;
    bool m_introDone = false;
    float m_introWait = 0.f;
    std::optional<bool> m_lastAccepting;
    std::optional<bool> m_lastRandomOrder;
    bool m_dotPulsing = false;

    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
    bool m_popupOnTop = false;
};

} // namespace paimon::twitch
