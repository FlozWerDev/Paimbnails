#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <unordered_set>
#include <vector>

class ButtonSprite;

namespace paimon::menumusic {

class MenuMusicLibraryPopup : public geode::Popup {
public:
    static MenuMusicLibraryPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;
    void keyDown(cocos2d::enumKeyCodes key, double timestamp) override;

    void buildHeader();
    void buildList();
    void rebuildList();

    void onSearchChanged(const std::string& query);
    void onPlayTrack(cocos2d::CCObject* sender);
    void onDownloadTrack(cocos2d::CCObject* sender);
    void onRemoveTrack(cocos2d::CCObject* sender);
    void onAddToPlaylist(cocos2d::CCObject* sender);
    void onToggleFavorite(cocos2d::CCObject* sender);
    void onToggleBlacklist(cocos2d::CCObject* sender);
    void onAddMusic(cocos2d::CCObject*);
    void onImportFolder(cocos2d::CCObject*);
    void onSyncGeometryDash(cocos2d::CCObject*);
    void onCycleSort(cocos2d::CCObject*);
    void onToggleLocalOnly(cocos2d::CCObject*);
    void onToggleFavoritesOnly(cocos2d::CCObject*);
    void onToggleBlacklistedOnly(cocos2d::CCObject*);
    void onToggleSortReverse(cocos2d::CCObject*);
    void onToggleCompact(cocos2d::CCObject*);
    void onScrollTop(cocos2d::CCObject*);
    void onScrollCurrent(cocos2d::CCObject*);
    void onScrollBottom(cocos2d::CCObject*);

    cocos2d::CCNode* buildTrackCard(
        const std::string& trackId, float widthOverride, float cardHeight);
    void refreshFilterButtons();

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_searchBar = nullptr;
    std::string m_query;
    std::string m_sortMode = "alphabetical";
    bool m_localOnly = false;
    bool m_favoritesOnly = false;
    bool m_blacklistedOnly = false;
    bool m_sortReverse = false;
    bool m_compact = false;
    ButtonSprite* m_sortSprite = nullptr;
    ButtonSprite* m_localSprite = nullptr;
    ButtonSprite* m_favoritesSprite = nullptr;
    ButtonSprite* m_blacklistedSprite = nullptr;
    ButtonSprite* m_reverseSprite = nullptr;
    ButtonSprite* m_compactSprite = nullptr;
    std::unordered_set<std::string> m_downloadingTrackIds;
    std::size_t m_libListenerToken = 0;
    std::size_t m_playerListenerToken = 0;
};

} // namespace paimon::menumusic
