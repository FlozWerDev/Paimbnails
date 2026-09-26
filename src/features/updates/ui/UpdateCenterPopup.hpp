#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include "../services/UpdateChecker.hpp"

namespace paimon::updates {

// update center: current state, update button, and full published-version
// history for rolling back to an older one.
class UpdateCenterPopup : public geode::Popup {
public:
    static UpdateCenterPopup* create();

protected:
    bool init();

    void buildHeader();
    void refreshHeader();
    void loadHistory();
    void rebuildHistory();
    cocos2d::CCNode* buildReleaseRow(ReleaseInfo const& release, int index);

    void showHistoryStatus(std::string const& text, bool retry);
    void showNotes(ReleaseInfo const& release);
    void confirmInstall(ReleaseInfo const& release);
    void startInstall(ReleaseInfo const& release);

    void onPrimary(cocos2d::CCObject*);
    void pollState(float dt);

private:
    cocos2d::CCMenu* m_headerMenu = nullptr;
    cocos2d::CCLabelBMFont* m_versionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    CCMenuItemSpriteExtra* m_primaryBtn = nullptr;

    cocos2d::CCNode* m_listHolder = nullptr;
    geode::ScrollLayer* m_historyScroll = nullptr;

    bool m_showPrereleases = false;
    bool m_historyFailed = false;

    UpdateChecker::State m_lastState = UpdateChecker::State::Idle;
    bool m_lastPending = false;
};

} // namespace paimon::updates
