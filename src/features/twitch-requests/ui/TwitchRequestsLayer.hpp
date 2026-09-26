#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geode { class ScrollLayer; }

namespace paimon::twitch {

struct LevelRequest;

class TwitchRequestsLayer : public cocos2d::CCLayer {
public:
    static TwitchRequestsLayer* create();
    static void open();

protected:
    ~TwitchRequestsLayer() override;

    bool init() override;
    void onEnterTransitionDidFinish() override;
    void keyBackClicked() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;

private:
    void buildBackground();
    void buildHeader();
    void buildSidePanel();
    void buildQueuePanel();
    void buildFooter();

    // deferred entry until the transition ends, or the animation plays out
    // behind the fade.
    void enterBy(cocos2d::CCNode* node, cocos2d::CCPoint offset, float delay, bool bounce = false);
    void runIntro();

    void tick(float dt);
    void refreshStatus();
    void refreshPercents();
    void rebuildRows();
    void scheduleRebuild();
    cocos2d::CCNode* buildRow(
        size_t index, size_t position, LevelRequest const& request, float width);

    bool inputsDiffer() const;
    void applyInputs();
    void onPrimary();
    void onPlatform();
    void applyPlatformSkin();
    void onToggleQueue();
    void onToggleOrder();
    // page mode: toggle and share your URL.
    void onToggleWebPage();
    void onCopyWebUrl();
    void onOpenWebUrl();
    void onFilters();
    void onNotify();
    void onStreamOverlay();
    void onPlayNext();
    void playRequest(size_t index);
    void openMessage(size_t index);
    void openVideo(std::string url);
    void onClearQueue();
    void onSettings();
    void onBack();

    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCNode* m_statusDot = nullptr;
    cocos2d::CCLabelBMFont* m_queueLabel = nullptr;
    cocos2d::CCLabelBMFont* m_hintLabel = nullptr;
    cocos2d::CCLabelBMFont* m_channelCaption = nullptr;
    cocos2d::CCLabelBMFont* m_commandCaption = nullptr;
    // page mode only: the address and its two buttons take the channel and
    // command fields' place.
    cocos2d::CCLabelBMFont* m_webUrlLabel = nullptr;
    cocos2d::CCMenu* m_webMenu = nullptr;
    cocos2d::CCSprite* m_background = nullptr;
    cocos2d::CCSprite* m_titleIcon = nullptr;
    geode::TextInput* m_channelInput = nullptr;
    geode::TextInput* m_commandInput = nullptr;
    ButtonSprite* m_primarySprite = nullptr;
    ButtonSprite* m_platformSprite = nullptr;
    ButtonSprite* m_queueSprite = nullptr;
    ButtonSprite* m_orderSprite = nullptr;
    cocos2d::CCNode* m_rowsHost = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;

    float m_listWidth = 0.f;
    float m_listHeight = 0.f;
    std::string m_lastStatus;
    std::string m_lastQueueText;
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
