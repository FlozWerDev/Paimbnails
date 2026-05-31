#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/utils/cocos.hpp>
#include <deque>
#include <functional>
#include <string>
#include <vector>

class SongSearchRowWidget;

/**
 * SongSearchPopup
 *
 * Lista scrolleable de canciones DESCARGADAS (Newgrounds + custom IDs > 0)
 * con búsqueda fuzzy por nombre y artista. Inspirado en matcool/song-search,
 * pero re-implementado in-house con `geode::Popup`, sin dependencia externa
 * de `fts::fuzzy_match` y adaptado al estilo visual de Paimbnails.
 *
 * Cuando el usuario selecciona una canción, llama al callback con el songID
 * y cierra el popup. ProfileMusicPopup escucha ese callback para precargar
 * el ID en el input principal y disparar `onLoadSong`.
 */
class SongSearchPopup : public geode::Popup, public TextInputDelegate {
public:
    using SelectCallback = std::function<void(int songID)>;

    static SongSearchPopup* create(SelectCallback callback);

    // Implementation details visibles para SongSearchRowWidget
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

    // TextInputDelegate (escuchamos cambios para hacer búsqueda en vivo)
    void textChanged(CCTextInputNode* input) override;

    // Soporte de scroll con touch + rueda
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void scrollWheel(float vertical, float horizontal) override;

    // Fuzzy match implementado in-house — devuelve true si query coincide en
    // target, con score (mayor = mejor match). Sin lib externa.
    static bool fuzzyMatch(std::string const& query, std::string const& target, int& outScore);

private:
    SelectCallback m_callback;
    geode::TextInput* m_searchInput = nullptr;
    cocos2d::CCNode* m_scrollContent = nullptr;
    cocos2d::CCLabelBMFont* m_resultsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;

    // Datos: pares (texto-indexable-en-lowercase, song)
    std::vector<std::pair<std::string, SongInfoObject*>> m_allDownloaded;
    // Resultado del filtro
    std::vector<SongInfoObject*> m_filtered;

    // Recycle de widgets para optimizar scroll
    std::deque<SongSearchRowWidget*> m_rowPool;
    float m_yScroll = 0.f;
    float m_prevYScroll = 0.f;

    // ID de la canción que actualmente está sonando como preview desde
    // este popup. Usado para mostrar el icono play/stop correcto. Si es
    // 0, no hay nada sonando desde aquí.
    int m_currentPreviewSongID = 0;

    // Constantes de layout
    static constexpr int   kVisibleRows  = 5;
    static constexpr float kRowWidth     = 320.f;
    static constexpr float kRowHeight    = 36.f;
    static constexpr float kRowSpacing   = 4.f;
};

/**
 * Widget interno: una fila con info de canción + botón play.
 * Click en el área principal selecciona la canción.
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
