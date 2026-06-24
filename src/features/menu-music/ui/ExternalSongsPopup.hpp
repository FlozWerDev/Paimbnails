#pragma once

// ExternalSongsPopup — lista scrollable con todas las canciones que el
// sistema menu-loop puede reproducir (config-dir + playlist + Newgrounds
// descargados por GD). Pensado para dar paridad con el "Song List" del
// mod de referencia `menuloop_randomizer` sin tocar la libreria propia
// del mod (MenuMusicLibrary).
//
// El popup vive como hijo del popup principal MenuMusicPopup, se abre
// desde el boton "Songs" de la fila de quick-actions y ofrece:
//   * Scroll con celdas compactas (path + filename)
//   * Buscador in-memory (contains, case-insensitive)
//   * Play por cancion (aplica override al menu-loop manager)
//   * Marca visual para la cancion actualmente sonando

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::menumusic {

class ExternalSongsPopup : public geode::Popup {
public:
    static ExternalSongsPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildHeader();
    void buildList();
    void rebuildList();

    // Callback: play a specific song (by its absolute path) as the
    // active menu-loop override.
    void playSongPath(const std::string& path);

    void onShuffleAll(cocos2d::CCObject*);
    void onSearchChanged(const std::string& query);
    void onPlayTapped(cocos2d::CCObject* sender);

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_searchBar = nullptr;
    cocos2d::CCLabelBMFont* m_summaryLabel = nullptr;
    std::string m_query;

    struct Row {
        std::string path;
        std::string label;
        std::string source;  // "menu-loop" | "downloaded" | "library"
    };
    std::vector<Row> m_rows;
};

} // namespace paimon::menumusic
