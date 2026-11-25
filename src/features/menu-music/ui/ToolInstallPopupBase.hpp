#pragma once

// shared chrome for the one-time tool download popups (yt-dlp, ffmpeg).
// both tools use the same layout with different strings, sizes, and fill
// colors; createForTool() below is the single entry point, parameterized by
// the config literals and the tool's bootstrap singleton.

#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace paimon::menumusic {

struct ToolInstallPopupConfig {
    float popupW = 400.f;
    float popupH = 220.f;
    const char* title = "";
    const char* infoTitle = "";
    const char* infoBody = "";
    const char* headline = "";
    float barW = 320.f;
    cocos2d::ccColor4B fillColor = {120, 220, 255, 255};
    const char* successMessage = "";
    // long save-dir paths are ellipsized past this width.
    std::size_t pathLimit = 60;
};

class ToolInstallPopupBase : public PaimonPopup {
public:
    // create + init + start the install for one tool. Boot is a bootstrap
    // singleton with bundledPath() (FfmpegBootstrap / YtDlpBootstrap);
    // runToolInstall<Boot> is explicitly instantiated in the .cpp.
    template <typename Boot>
    static ToolInstallPopupBase* createForTool(ToolInstallPopupConfig const& config,
                                               Boot& boot,
                                               std::function<void(bool)> onFinished) {
        auto ret = new ToolInstallPopupBase();
        if (ret && ret->initBase(config, boot.bundledPath(), std::move(onFinished))) {
            ret->runToolInstall(boot);
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

protected:
    bool initBase(ToolInstallPopupConfig const& config,
                  std::filesystem::path const& destPath,
                  std::function<void(bool)> onFinished);
    void onExit() override;

    void updateProgress(float pct, std::string const& message);
    void setStatusMessage(std::string const& message);
    void finishSuccess();
    void finishError(std::string const& error);

    // S2: single chokepoint for the one-time tool install flow. Both tool
    // popups only differ in bootstrap/progress types; the alive/shutdown
    // guards and the main-thread hops live here exactly once. Defined in
    // the .cpp with explicit instantiations per bootstrap.
    template <typename Boot>
    void runToolInstall(Boot& boot);
    void onDismiss(cocos2d::CCObject*);

    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_percentLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pathLabel = nullptr;
    cocos2d::CCSize m_statusBounds;

    cocos2d::CCNode* m_barBg = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;

    CCMenuItemSpriteExtra* m_dismissBtn = nullptr;

    std::string m_successMessage;
    std::function<void(bool)> m_onFinished;
    bool m_finished = false;
    // alive-token shared with async callbacks: captured by value so they can
    // check liveness after the popup is destroyed.
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};

} // namespace paimon::menumusic
