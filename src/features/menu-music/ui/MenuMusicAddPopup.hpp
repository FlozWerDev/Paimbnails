#pragma once

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class MenuMusicAddPopup : public geode::Popup {
public:
    static MenuMusicAddPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildUrlSection();
    void buildLocalSection();
    void buildProgressBar();
    void refreshStatus();

    void setProgressBarVisible(bool visible);
    void updateProgressBar(float ratio01);

    void onPickAudio(cocos2d::CCObject*);
    void onPickCover(cocos2d::CCObject*);
    void onImportLocal(cocos2d::CCObject*);
    void onStartDownload(cocos2d::CCObject*);
    void onOpenYtDlpHelp(cocos2d::CCObject*);
    void onPasteUrl(cocos2d::CCObject*);

    void finalizeLocalImport();

    geode::TextInput* m_urlInput = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    cocos2d::CCLabelBMFont* m_audioPathLabel = nullptr;
    cocos2d::CCLabelBMFont* m_coverPathLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_ytDlpLabel = nullptr;

    // Visible progress bar for yt-dlp downloads. Many users didn't realize
    // the download was running because we only updated a small text label.
    cocos2d::CCLayerColor* m_progressBarBg = nullptr;
    cocos2d::CCLayerColor* m_progressBarFill = nullptr;
    cocos2d::CCLabelBMFont* m_progressPercentLabel = nullptr;

    std::string m_pendingAudioPath;
    std::string m_pendingCoverPath;
    bool m_busy = false;
    std::atomic<bool> m_alive{true};
};

} // namespace paimon::menumusic
