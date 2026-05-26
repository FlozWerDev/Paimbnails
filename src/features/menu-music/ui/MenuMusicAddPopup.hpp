#pragma once

// MenuMusicAddPopup — dos formas de anadir musica:
//   1. Importar un archivo local (audio + opcional cover) via file picker.
//   2. Descargar desde URL via yt-dlp — descarga audio Y miniatura, mueve
//      la miniatura a covers dir y registra el track automaticamente.
//
// Si yt-dlp no esta disponible mostramos un mensaje claro con el path
// sugerido donde colocar el binario.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <string>

namespace paimon::menumusic {

class MenuMusicAddPopup : public geode::Popup {
public:
    static MenuMusicAddPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildUrlSection();
    void buildLocalSection();
    void refreshStatus();

    // Callbacks
    void onPickAudio(cocos2d::CCObject*);
    void onPickCover(cocos2d::CCObject*);
    void onImportLocal(cocos2d::CCObject*);
    void onStartDownload(cocos2d::CCObject*);
    void onOpenYtDlpHelp(cocos2d::CCObject*);
    void onPasteUrl(cocos2d::CCObject*);

    // Worker helper: crea el track final a partir de los paths pendientes
    // de importacion local. Mueve los archivos dentro del save dir.
    void finalizeLocalImport();

    geode::TextInput* m_urlInput = nullptr;
    geode::TextInput* m_nameInput = nullptr;
    cocos2d::CCLabelBMFont* m_audioPathLabel = nullptr;
    cocos2d::CCLabelBMFont* m_coverPathLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_ytDlpLabel = nullptr;

    std::string m_pendingAudioPath;
    std::string m_pendingCoverPath;
    bool m_busy = false;
    std::atomic<bool> m_alive{true};
};

} // namespace paimon::menumusic
