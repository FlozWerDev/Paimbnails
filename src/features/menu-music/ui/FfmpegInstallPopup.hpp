#pragma once

// FfmpegInstallPopup — popup modal con barra de progreso para la descarga
// inicial del binario de ffmpeg.
//
// Se muestra cuando el usuario acepta la pregunta "descargar ffmpeg?" en
// el flujo de importacion desde URL. Identico en comportamiento al
// YtDlpInstallPopup, pero apunta al FfmpegBootstrap y al archivo final
// es mas grande (~80 MB), asi que avisa del tamano en el label.

#include <Geode/Geode.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace paimon::menumusic {

class FfmpegInstallPopup : public geode::Popup {
public:
    // `onFinished(true)` cuando el binario quedo instalado.
    // `onFinished(false)` si hubo error o se cerro sin exito.
    // El callback se invoca siempre en main thread; puede ser nullptr.
    static FfmpegInstallPopup* create(std::function<void(bool)> onFinished);

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
    // Shared alive-token: captured by value in async callbacks so they can
    // safely check liveness even after this popup has been destroyed (the
    // bootstrap keeps the callback alive past the popup's lifetime).
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};

} // namespace paimon::menumusic
