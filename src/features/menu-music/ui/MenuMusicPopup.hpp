#pragma once

#include <Geode/Geode.hpp>
#include <string>

// Forward decl for the cocos Slider class from Geode bindings.
class Slider;
class ButtonSprite;

namespace paimon::menumusic {

class VinylDisc;
class CoverBlurBackground;
class CoverHero;

class MenuMusicPopup : public geode::Popup {
public:
    static MenuMusicPopup* create();

protected:
    bool init(float width, float height);
    void onEnterTransitionDidFinish() override;
    void onExit() override;

    void buildFullBackground();
    void buildContentClipper();
    void buildBlurBackground();
    void buildFullscreenBackdrop();
    void buildTopBar();
    void buildVinyl();
    void buildInfoColumn();
    void buildTransport();
    void buildSeekBar();
    void buildActions();
    void buildModeSelector();
    void buildBottomBar();

    void refreshFromState();
    void updateModeSelector();
    void onTrackChanged(const std::string& trackId);
    void onLibraryChanged();

    void onPlayPause(cocos2d::CCObject*);
    void onPrev(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onShuffle(cocos2d::CCObject*);
    void onModeLibrary(cocos2d::CCObject*);
    void onModePlaylist(cocos2d::CCObject*);
    void onModeDisabled(cocos2d::CCObject*);
    void onOpenLibrary(cocos2d::CCObject*);
    void onOpenPlaylists(cocos2d::CCObject*);
    void onOpenExternalSongs(cocos2d::CCObject*);
    void onOpenTags(cocos2d::CCObject*);
    void onOpenAdd(cocos2d::CCObject*);
    void onOpenSettings(cocos2d::CCObject*);
    void onHold(cocos2d::CCObject*);
    void onFavorite(cocos2d::CCObject*);
    void onBlacklist(cocos2d::CCObject*);
    void onCopy(cocos2d::CCObject*);
    void onAddCurrentToPlaylist(cocos2d::CCObject*);
    void onReplayToast(cocos2d::CCObject*);

    void onSeekBackward(cocos2d::CCObject*);
    void onSeekForward(cocos2d::CCObject*);
    void onSeekSliderChanged(cocos2d::CCObject*);
    void tickSeekUpdate(float dt);
    void pressAndHoldSeek(float dt);

    void keyDown(cocos2d::enumKeyCodes key, double p1) override;

    struct DetectedSong {
        std::string displayName;
        std::string artist;
        std::string coverPath;
        std::vector<std::string> coverPaths;
        std::string audioPath;
        int songID = 0;
        bool isPaimonTrack = false;
        bool hasAnything = false;
    };
    DetectedSong detectActiveSong(bool logResult = true) const;

    void applyCovers(std::vector<std::string> const& coverPaths);

    void applyFullscreenCover(const std::string& coverPath);
    void syncCoverChrome(float dt);

    void pollExternalSong(float dt);

    CoverBlurBackground* m_bg = nullptr;
    VinylDisc* m_disc = nullptr;
    CoverHero* m_hero = nullptr;
    cocos2d::CCClippingNode* m_contentClip = nullptr;

    cocos2d::CCDrawNode* m_fullBg = nullptr;

    cocos2d::CCNode* m_fullscreenBackdrop = nullptr;
    cocos2d::CCLayerColor* m_fullscreenDim = nullptr;
    cocos2d::CCSprite* m_fullscreenBlur = nullptr;
    std::uint64_t m_fullscreenBlurGen = 0;
    std::string m_lastCoverPath;
    std::string m_lastChromeCoverPath;
    std::vector<std::string> m_activeCoverPaths;
    std::string m_lastDetectedPath;

    cocos2d::CCLabelBMFont* m_trackLabel = nullptr;
    cocos2d::CCClippingNode* m_trackClip = nullptr;
    float m_trackClipWidth = 0.f;
    cocos2d::CCLabelBMFont* m_subtitleLabel = nullptr;

    // Selector de modo (Off / All Songs / Playlist): el activo se resalta.
    ButtonSprite* m_modeOffSpr = nullptr;
    ButtonSprite* m_modeAllSpr = nullptr;
    ButtonSprite* m_modePlaylistSpr = nullptr;
    ButtonSprite* m_holdActionSpr = nullptr;
    ButtonSprite* m_favoriteActionSpr = nullptr;
    ButtonSprite* m_blacklistActionSpr = nullptr;

    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    cocos2d::CCSprite* m_playSprite = nullptr;
    cocos2d::CCSprite* m_pauseSprite = nullptr;

    cocos2d::CCDrawNode* m_seekTrack = nullptr;
    cocos2d::CCDrawNode* m_seekFill = nullptr;
    Slider* m_seekSlider = nullptr;
    bool m_seekSliderDragging = false;
    cocos2d::CCLabelBMFont* m_seekCurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_seekTotalLabel = nullptr;
    CCMenuItemSpriteExtra* m_seekBkwdBtn = nullptr;
    CCMenuItemSpriteExtra* m_seekFwrdBtn = nullptr;
    float m_seekHoldTime = 0.f;
    cocos2d::CCNode* m_seekRow = nullptr;
    cocos2d::CCSize m_seekFillMaxSize {0.f, 0.f};

    std::size_t m_libListenerToken = 0;
    std::size_t m_playerListenerToken = 0;
    int m_pendingSongCoverID = 0;
    int m_failedSongCoverID = 0;
};

} // namespace paimon::menumusic
