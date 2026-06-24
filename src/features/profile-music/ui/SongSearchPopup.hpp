#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class SongSearchRowWidget;

/**
 * SongSearchPopup
 *
 * Scrollable list of downloaded songs (Newgrounds + custom IDs > 0) with fuzzy
 * search by name and artist. On selection, calls the callback with the songID
 * and closes; ProfileMusicPopup uses it to preload the ID and trigger onLoadSong.
 */
class SongSearchPopup : public geode::Popup, public TextInputDelegate {
public:
    using SelectCallback = std::function<void(int songID)>;

    static SongSearchPopup* create(SelectCallback callback);

    // Implementation details exposed to SongSearchRowWidget
    void onSongSelected(int songID);
    void onSongPreview(SongInfoObject* song);
    void refreshAllRowsPlayState();
    int  getCurrentPreviewSongID() const { return m_currentPreviewSongID; }

protected:
    bool init(SelectCallback callback);

    void rebuildScrollList();
    void updateScrollLayout(bool forceRefresh);
    void runSearch();
    void onClose(cocos2d::CCObject*) override;

    // TextInputDelegate (listen for changes to search live)
    void textChanged(CCTextInputNode* input) override;

    // Touch + wheel scroll support
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void scrollWheel(float vertical, float horizontal) override;

    // In-house fuzzy match — true if query matches target, with score (higher = better).
    static bool fuzzyMatch(std::string const& query, std::string const& target, int& outScore);

private:
    SelectCallback m_callback;
    geode::TextInput* m_searchInput = nullptr;
    cocos2d::CCNode* m_scrollContent = nullptr;
    cocos2d::CCLabelBMFont* m_resultsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    // Data: pairs of (lowercase indexable text, song)
    std::vector<std::pair<std::string, SongInfoObject*>> m_allDownloaded;
    // Filter result
    std::vector<SongInfoObject*> m_filtered;

    // Widget recycling to optimize scroll
    std::deque<SongSearchRowWidget*> m_rowPool;
    float m_yScroll = 0.f;
    float m_prevYScroll = 0.f;

    // SongID currently previewing from this popup (0 = none); drives the play/stop icon.
    int m_currentPreviewSongID = 0;

    // Layout constants
    static constexpr int   kVisibleRows  = 5;
    static constexpr float kRowWidth     = 320.f;
    static constexpr float kRowHeight    = 36.f;
    static constexpr float kRowSpacing   = 4.f;
};

/**
 * Internal widget: a row with song info + play button.
 * Clicking the main area selects the song.
 */
class SongSearchRowWidget : public cocos2d::CCLayer {
public:
    static SongSearchRowWidget* create(SongSearchPopup* parent);
    void setSong(SongInfoObject* song);
    void updatePlayButton();
    SongInfoObject* getSong() const { return m_song; }

protected:
    bool init(SongSearchPopup* parent);
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void onPlayClicked(cocos2d::CCObject*);
    void onSelectClicked(cocos2d::CCObject*);

private:
    SongSearchPopup*               m_parent      = nullptr;
    SongInfoObject*                m_song        = nullptr;
    cocos2d::CCLabelBMFont*        m_nameLabel   = nullptr;
    cocos2d::CCLabelBMFont*        m_artistLabel = nullptr;
    CCMenuItemSpriteExtra*         m_playButton  = nullptr;
    CCMenuItemSpriteExtra*         m_selectButton = nullptr;
    bool                           m_touchInside = false;
};
