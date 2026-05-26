#pragma once

// YtDlpInstallPopup — popup modal con barra de progreso para la descarga
// inicial del binario yt-dlp.
//
// Se muestra cuando el usuario pulsa "Download" en MenuMusicAddPopup y el
// binario todavia no esta instalado. Muestra los MB descargados en tiempo
// real y una barra de progreso que refleja los bytes recibidos. Al
// terminar con exito se cierra y llama a `m_onFinished(true)` para que
// el popup padre continue con la descarga real del audio.
//
// Destino: [saveDir]/yt-dlp/yt-dlp.exe — dentro de la carpeta de datos
// del mod (gestionada por Geode). Si el usuario desinstala el mod con
// la opcion "delete data", el binario desaparece automaticamente.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <atomic>
#include <functional>
#include <string>

namespace paimon::menumusic {

class YtDlpInstallPopup : public geode::Popup {
public:
    // `onFinished(true)` cuando el binario quedo instalado.
    // `onFinished(false)` si hubo error o se cerro sin exito.
    // El callback se invoca siempre en main thread; puede ser nullptr.
    static YtDlpInstallPopup* create(std::function<void(bool)> onFinished);

protected:
    bool init(std::function<void(bool)> onFinished);
    void onExit() override;

    void startInstall();
    void updateProgress(float pct, const std::string& message);
    void finishSuccess();
    void finishError(const std::string& error);
    void onDismiss(cocos2d::CCObject*);

    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_percentLabel = nullptr;
    cocos2d::CCLabelBMFont* m_pathLabel = nullptr;

    cocos2d::CCNode* m_barBg = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;

    CCMenuItemSpriteExtra* m_dismissBtn = nullptr;

    std::function<void(bool)> m_onFinished;
    bool m_finished = false;
    bool m_success = false;
    std::atomic<bool> m_alive{true};
};

} // namespace paimon::menumusic
