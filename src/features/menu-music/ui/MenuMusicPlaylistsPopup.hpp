#pragma once

// MenuMusicPlaylistsPopup — administrador de playlists.
// Dos modos:
//   * Grid de playlists (default): card por playlist con nombre + count
//     + 3 acciones (Activate, Rename, Delete).
//   * Detalle de playlist: lista de tracks con drag-off rapido. Hay un
//     back button para volver al grid.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class MenuMusicPlaylistsPopup : public geode::Popup {
public:
    static MenuMusicPlaylistsPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildHeader();
    void buildList();

    void showGrid();
    void showDetail(const std::string& playlistId);

    void onCreatePlaylist(cocos2d::CCObject*);
    void onActivatePlaylist(cocos2d::CCObject* sender);
    void onOpenPlaylist(cocos2d::CCObject* sender);
    void onDeletePlaylist(cocos2d::CCObject* sender);
    void onRemoveFromPlaylist(cocos2d::CCObject* sender);
    void onBackToGrid(cocos2d::CCObject*);

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    cocos2d::CCMenu* m_backMenu = nullptr;
    std::string m_detailPlaylistId;
    bool m_inDetail = false;
    std::size_t m_libListenerToken = 0;
};

} // namespace paimon::menumusic
