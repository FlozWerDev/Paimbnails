#pragma once

#include "../services/NewgroundsCatalog.hpp"

#include <Geode/Geode.hpp>
#include <chrono>

namespace FMOD {
class Channel;
class Sound;
}

namespace paimon::menumusic {

class NewgroundsBrowserPopup : public geode::Popup {
public:
    static NewgroundsBrowserPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildHeader();
    void buildList();
    void loadWeekly();
    void runSearch();
    void showResult(NewgroundsListResult result);
    void rebuildList();
    void refreshStatus();
    void updatePreviewButtons();
    void startPreviewStream(std::string const& url);
    void pollPreview(float);
    void stopPreview();

    void onSearch(cocos2d::CCObject*);
    void onWeekly(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);
    void onCopyTrackId(cocos2d::CCObject* sender);
    void onPreviewTrack(cocos2d::CCObject* sender);
    void onDownloadTrack(cocos2d::CCObject* sender);

    NewgroundsTrack const* trackById(int songId) const;

    geode::TextInput* m_searchInput = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    std::vector<NewgroundsTrack> m_tracks;
    std::vector<CCMenuItemSpriteExtra*> m_previewButtons;
    std::string m_listTitle;
    std::string m_emptyMessage;
    int m_previewSongId = 0;
    FMOD::Sound* m_previewSound = nullptr;
    FMOD::Channel* m_previewChannel = nullptr;
    std::chrono::steady_clock::time_point m_previewOpenStarted{};
    int m_requestGeneration = 0;
    int m_previewRequestGeneration = 0;
    bool m_loading = false;
    bool m_backgroundPauseCaptured = false;
    bool m_backgroundWasPaused = false;
};

} // namespace paimon::menumusic
