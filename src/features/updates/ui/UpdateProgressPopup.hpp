#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <functional>
#include <string>

namespace paimon::updates {

// popup with .geode download progress + "restart" button when done.
class UpdateProgressPopup : public geode::Popup {
public:
    // downloads one version from the history; oninstalled tells the update
    // center to refresh its buttons.
    static UpdateProgressPopup* create(
        std::string url, std::string version, std::function<void()> onInstalled = nullptr
    );

protected:
    bool init(std::string url, std::string version, std::function<void()> onInstalled);

    void startDownload();
    void onProgress(uint64_t received, uint64_t total);
    void onDone(bool ok, std::string const& msgOrPath);

    void onCancel(cocos2d::CCObject* sender);
    void onRestart(cocos2d::CCObject* sender);
    void onClose(cocos2d::CCObject* sender) override;

private:
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_percentLabel = nullptr;
    cocos2d::CCNode* m_barBg = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;

    cocos2d::CCMenu* m_actionMenu = nullptr;
    CCMenuItemSpriteExtra* m_cancelBtn = nullptr;
    CCMenuItemSpriteExtra* m_restartBtn = nullptr;

    std::string m_url;
    std::string m_version;
    std::function<void()> m_onInstalled;

    bool m_finished = false;
};

} // namespace paimon::updates
