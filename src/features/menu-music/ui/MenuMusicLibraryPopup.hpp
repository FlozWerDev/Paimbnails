#pragma once

// MenuMusicLibraryPopup — scrollable track list with search and per-track actions
// (play, remove, add-to-playlist). Compact tiles with thumbnail + name + source chip
// (Local/Downloaded) instead of the dense orange list of the reference mod.

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::menumusic {

class MenuMusicLibraryPopup : public geode::Popup {
public:
    static MenuMusicLibraryPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildHeader();
    void buildList();
    void rebuildList();

    void onSearchChanged(const std::string& query);
    void onPlayTrack(cocos2d::CCObject* sender);
    void onRemoveTrack(cocos2d::CCObject* sender);
    void onAddToPlaylist(cocos2d::CCObject* sender);
    void onAddMusic(cocos2d::CCObject*);

    // Helper: construye una "tarjeta" tile para un track dado.
    cocos2d::CCNode* buildTrackCard(const std::string& trackId, float widthOverride);

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_searchBar = nullptr;
    std::string m_query;
    std::size_t m_libListenerToken = 0;
};

} // namespace paimon::menumusic
