#pragma once

// MenuMusicPopup — popup principal del sistema Menu Music.
//
// Layout rectangular compacto (460x210):
//   [ cover blur como fondo, ocupa todo ]
//   ┌──────────────────────────────────────────────────────┐
//   │  MENU MUSIC               ┌Title (clipped)─────────┐ │
//   │                           │ Downloaded track        │ │
//   │  ┌───────┐   ◀  ▶/⏸  ▶  🔀                        │ │
//   │  │ HERO  │   0:15 ━━━●━━━━━━ 3:20  [-5s] [+5s]     │ │
//   │  │ COVER │   ⭐  ❌  ⏸  📋  🔁  ➕                  │ │
//   │  └───────┘                                           │ │
//   │  [Library] [Playlists] [Add] [Random All]            │ │
//   └──────────────────────────────────────────────────────┘
//
// Usa:
//   * CoverBlurBackground para el fondo
//   * CoverHero / VinylDisc para el disco
//   * Slider de Geode para el seek bar (drag libre)

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <string>

// Forward decl for the cocos Slider class from Geode bindings.
class Slider;

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

    // ── Build ──
    void buildFullBackground();
    void buildContentClipper();
    void buildBlurBackground();
    void buildFullscreenBackdrop();
    void buildTopBar();
    void buildVinyl();
    void buildInfoColumn();
    void buildTransport();
    void buildSeekBar();
    void buildQuickActions();
    void buildBottomBar();

    // ── Refresh ──
    void refreshFromState();
    void onTrackChanged(const std::string& trackId);
    void onLibraryChanged();

    // ── Button callbacks ──
    void onPlayPause(cocos2d::CCObject*);
    void onPrev(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onShuffle(cocos2d::CCObject*);       // shuffle sobre Library
    void onShuffleAll(cocos2d::CCObject*);    // shuffle sobre Library + menu-loop + GD downloaded
    void onModeLibrary(cocos2d::CCObject*);
    void onModePlaylist(cocos2d::CCObject*);
    void onModeDisabled(cocos2d::CCObject*);
    void onOpenLibrary(cocos2d::CCObject*);
    void onOpenPlaylists(cocos2d::CCObject*);
    void onOpenAdd(cocos2d::CCObject*);

    // ── Quick actions (parity with Menu Loop Randomizer) ──
    // Actuan sobre el sistema menu-loop (songs externas, override, etc.)
    // y sobre el player del mod segun lo que este sonando. Las acciones
    // que afectan al estado global (favorite/blacklist/previous) siempre
    // usan el MenuLoopControl porque es el que gestiona la persistencia.
    void onFavorite(cocos2d::CCObject*);
    void onBlacklist(cocos2d::CCObject*);
    void onHold(cocos2d::CCObject*);
    void onCopyName(cocos2d::CCObject*);
    void onRegenNotification(cocos2d::CCObject*);
    void onAddToPlaylistFile(cocos2d::CCObject*);
    void onOpenSongs(cocos2d::CCObject*);

    // ── Playback progress controls ──
    void onSeekBackward(cocos2d::CCObject*);
    void onSeekForward(cocos2d::CCObject*);
    void onSeekSliderChanged(cocos2d::CCObject*);
    void tickSeekUpdate(float dt);

    // ── Keyboard shortcuts (YT/VLC/Spotify-like) ──
    void keyDown(cocos2d::enumKeyCodes key, double p1) override;

    // ── External song detection ──
    struct DetectedSong {
        std::string displayName;
        std::string artist;
        std::string coverPath;
        std::string audioPath;
        bool isPaimonTrack = false;  // true si viene de MenuMusicLibrary
        bool hasAnything = false;    // true si al menos hay algo sonando
    };
    DetectedSong detectActiveSong() const;

    // ── Fullscreen backdrop (blur de la cover ocupando toda la pantalla) ──
    void applyFullscreenCover(const std::string& coverPath);

    // ── Ticker para detectar cambios del menu-loop vanilla ──
    void pollExternalSong(float dt);

    CoverBlurBackground* m_bg = nullptr;
    VinylDisc* m_disc = nullptr;
    CoverHero* m_hero = nullptr;
    cocos2d::CCClippingNode* m_contentClip = nullptr;

    // Fondo oscuro que cubre todo m_mainLayer para que no se vean
    // huecos del frame vanilla de GD entre el clipper y los bordes.
    cocos2d::CCDrawNode* m_fullBg = nullptr;

    // Fullscreen backdrop que vive como hijo directo de `this` detras
    // del m_mainLayer.
    cocos2d::CCNode* m_fullscreenBackdrop = nullptr;
    cocos2d::CCLayerColor* m_fullscreenDim = nullptr;
    cocos2d::CCSprite* m_fullscreenBlur = nullptr;
    std::uint64_t m_fullscreenBlurGen = 0;
    std::string m_lastCoverPath;
    std::string m_lastDetectedPath;

    cocos2d::CCLabelBMFont* m_trackLabel = nullptr;
    cocos2d::CCClippingNode* m_trackClip = nullptr;
    float m_trackClipWidth = 0.f;
    cocos2d::CCLabelBMFont* m_subtitleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;

    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    cocos2d::CCSprite* m_playSprite = nullptr;
    cocos2d::CCSprite* m_pauseSprite = nullptr;

    // ── Playback progress UI ──
    cocos2d::CCDrawNode* m_seekTrack = nullptr;
    cocos2d::CCDrawNode* m_seekFill = nullptr;
    Slider* m_seekSlider = nullptr;
    bool m_seekSliderDragging = false;
    cocos2d::CCLabelBMFont* m_seekCurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_seekTotalLabel = nullptr;
    CCMenuItemSpriteExtra* m_seekBkwdBtn = nullptr;
    CCMenuItemSpriteExtra* m_seekFwrdBtn = nullptr;
    cocos2d::CCNode* m_seekRow = nullptr;
    cocos2d::CCSize m_seekFillMaxSize {0.f, 0.f};

    std::size_t m_libListenerToken = 0;
    std::size_t m_playerListenerToken = 0;
};

} // namespace paimon::menumusic
