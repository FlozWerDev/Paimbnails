#pragma once
#include "../../../ui/PaimonPopup.hpp"

// Full native-GD Editor Music Menu (opened from the floating Ctrl+M panel,
// editor toolbar button, or EditorPauseLayer).

#include "../services/EditorMusicPlayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class Slider;

namespace paimon::editormusic {

class EditorMusicPickerPopup : public PaimonPopup {
public:
    static EditorMusicPickerPopup* create(int initialTab = 0);

private:
    enum class SongFilter : std::uint8_t { All = 0, Favorites, GeometryDash, Local, Count };
    enum class SongSort : std::uint8_t { Alphabetical = 0, Recent, Duration, Count };

    bool init(int initialTab);
    void onExit() override;
    void onClose(cocos2d::CCObject* sender) override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;
    bool scrollWheel(float x, float y) override;

    void buildHeaderDeck();
    void buildTabBar();
    void buildSongsTab(float contentTop, float contentBottom);
    void buildPlaylistsTab(float contentTop, float contentBottom);
    void buildEffectsTab(float contentTop, float contentBottom);
    void buildSettingsTab(float contentTop, float contentBottom);
    void rebuildTabContent();
    void rebuildSongsList(bool preserveScroll);
    void scheduleRebuild();
    void scheduleSongsListRebuild(bool preserveScroll);
    void updateSongFilterButtons();

    bool isTrackPlayable(std::string const& audioPath) const;
    cocos2d::CCNode* trackRow(float width, std::string const& trackId);
    std::vector<std::string> filteredLibraryTracks() const;
    void syncHeaderFromState();
    void tick(float dt);
    void loadHeaderCover(std::string const& path, int songId);
    void applyHeaderCover(cocos2d::CCTexture2D* tex);

    void onSeekSliderChanged(cocos2d::CCObject*);
    void onVolumeSliderChanged(cocos2d::CCObject*);
    void syncDownloads();
    void importFolder();
    void openAddMusicPopup();
    void openNewgroundsBrowser();
    void createPlaylistFromInput();

    int m_tab = 0;
    SongFilter m_filter = SongFilter::All;
    SongSort m_sort = SongSort::Alphabetical;
    std::string m_search;
    std::string m_newPlaylistName;

    bool m_closing = false;
    bool m_rebuildPending = false;
    bool m_songsListRebuildPending = false;
    bool m_songsListPreserveScroll = false;
    bool m_seekDragging = false;
    bool m_seekCommitPending = false;
    float m_seekPendingRatio = 0.f;
    bool m_volDragging = false;
    bool m_volCommitPending = false;
    int m_lastSeekPosSec = -1;
    int m_lastSeekLenSec = -1;
    float m_clock = 0.f;
    float m_spinAngle = 0.f;
    float m_spinVel = 0.f;
    std::array<float, 5> m_eqLevels = {0.2f, 0.3f, 0.4f, 0.28f, 0.18f};

    std::size_t m_playerToken = 0;
    std::size_t m_libToken = 0;

    mutable std::unordered_map<std::string, bool> m_playableCache;

    // Header deck widgets
    cocos2d::CCNode* m_headerDeck = nullptr;
    cocos2d::CCNode* m_discRotor = nullptr;
    cocos2d::CCClippingNode* m_coverClip = nullptr;
    cocos2d::CCSprite* m_coverSprite = nullptr;
    cocos2d::CCSprite* m_coverFallback = nullptr;
    std::string m_loadedCoverPath;
    std::uint64_t m_coverGen = 0;

    cocos2d::CCLabelBMFont* m_nowTitle = nullptr;
    cocos2d::CCLabelBMFont* m_nowSub = nullptr;
    cocos2d::CCLabelBMFont* m_deckMetaLabel = nullptr;
    cocos2d::CCLabelBMFont* m_seekCurLabel = nullptr;
    cocos2d::CCLabelBMFont* m_seekTotLabel = nullptr;
    Slider* m_seekSlider = nullptr;
    Slider* m_volSlider = nullptr;

    CCMenuItemSpriteExtra* m_playBtn = nullptr;
    CCMenuItemSpriteExtra* m_favBtn = nullptr;
    CCMenuItemSpriteExtra* m_muteBtn = nullptr;
    cocos2d::CCSprite* m_shuffleIcon = nullptr;
    cocos2d::CCSprite* m_repeatIcon = nullptr;
    cocos2d::CCSprite* m_favIcon = nullptr;
    cocos2d::CCLabelBMFont* m_repeatBadge = nullptr;
    std::array<geode::NineSlice*, 5> m_eqBars = {};

    std::string m_shownTrackId;
    bool m_shownPlaying = false;
    bool m_shownFav = false;
    bool m_shownMuted = false;

    // Snapshot of state rendered in the current list so volume/shuffle drags don't rebuild 100+ rows.
    std::string m_renderedTrackId;
    bool m_renderedPlaying = false;
    EditorQueueSource m_renderedSource = EditorQueueSource::All;
    std::string m_renderedPlaylistId;
    std::size_t m_renderedQueueSize = 0;

    cocos2d::CCNode* m_tabBarContainer = nullptr;
    cocos2d::CCNode* m_tabContent = nullptr;
    cocos2d::CCNode* m_songsListHolder = nullptr;
    CCMenuItemSpriteExtra* m_filterBtn = nullptr;
    CCMenuItemSpriteExtra* m_sortBtn = nullptr;
    cocos2d::CCSize m_songsListSize{};
    geode::ScrollLayer* m_activeScroll = nullptr;
    float m_scrollTargetY = 0.f;
    bool m_scrollTargetSet = false;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
};

} // namespace paimon::editormusic
