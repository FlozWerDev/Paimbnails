#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// QuickHubKeybind — detecta el hold de Ctrl para abrir el Quick Hub Radial.
//
// Implementacion (cross-platform):
//   - Hookeamos CCKeyboardDispatcher::updateModifierKeys(shft, ctrl, alt, cmd).
//     Este metodo se llama SIEMPRE que cambia el estado de un modificador en
//     todas las plataformas (Windows, macOS, Android, iOS). Es mas confiable
//     que hookear dispatchKeyboardMSG, donde Ctrl puro puede llegar como
//     KEY_Control (0x11), KEY_LeftControl (0xA2) o KEY_RightContol (0xA3)
//     dependiendo de la plataforma — o incluso filtrarse antes de llegar al
//     delegate (ver CCKeyboardDispatcher.cpp en iOS, donde se hace early-return
//     en el switch para KEY_Shift/Control/Alt).
//
// Flujo:
//   1. Ctrl down → inicia timer (0.5s dead zone + 1.0s fill)
//   2. A los 0.5s aparece la barra de progreso
//   3. A los 1.5s se abre el radial
//   4. Ctrl up → cancela si no se completo, o deja el radial abierto
//
// Limitaciones:
//   - Si el usuario presiona Ctrl + cualquier otra tecla (Ctrl+S, Ctrl+P, etc.)
//     antes de que el hold se complete, dejamos que esa otra tecla cancele el
//     hold (porque obviamente esta usando un atajo, no un hold).

namespace {

// Tiempos en segundos
constexpr float kDeadZone = 0.5f;      // tiempo antes de mostrar la barra
constexpr float kFillDuration = 1.0f;  // tiempo que tarda en llenarse la barra
constexpr float kTotalHold = kDeadZone + kFillDuration; // 1.5s total

// Estado global del hold
struct HoldState {
    bool ctrlDown = false;
    float elapsed = 0.f;
    bool barVisible = false;
    bool radialOpened = false;
    bool cancelledByOtherKey = false; // si el user pulso otra tecla mientras tenia ctrl
    cocos2d::CCNode* progressBar = nullptr;
    cocos2d::CCNode* progressFill = nullptr;
};

static HoldState s_hold;

void cleanupProgressBar() {
    if (s_hold.progressBar) {
        s_hold.progressBar->removeFromParent();
        s_hold.progressBar = nullptr;
        s_hold.progressFill = nullptr;
    }
    s_hold.barVisible = false;
}

void createProgressBar() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::get()->getWinSize();

    // Contenedor de la barra
    auto container = CCNode::create();
    container->setPosition({winSize.width / 2.f, winSize.height - 6.f});
    container->setContentSize({200.f, 4.f});
    container->setAnchorPoint({0.5f, 0.5f});
    container->setZOrder(99999);
    scene->addChild(container);

    // Fondo de la barra (gris oscuro)
    auto bg = CCLayerColor::create({40, 40, 50, 180});
    bg->setContentSize({200.f, 4.f});
    bg->setPosition({-100.f, -2.f});
    container->addChild(bg, 0);

    // Fill de la barra (blanco)
    auto fill = CCLayerColor::create({255, 255, 255, 220});
    fill->setContentSize({0.f, 4.f});
    fill->setPosition({-100.f, -2.f});
    container->addChild(fill, 1);

    s_hold.progressBar = container;
    s_hold.progressFill = fill;
    s_hold.barVisible = true;
}

void updateProgressBar(float progress) {
    if (!s_hold.progressFill) return;
    float maxW = 200.f;
    float w = maxW * std::clamp(progress, 0.f, 1.f);
    s_hold.progressFill->setContentSize({w, 4.f});
}

void resetHold() {
    s_hold.ctrlDown = false;
    s_hold.elapsed = 0.f;
    s_hold.radialOpened = false;
    s_hold.cancelledByOtherKey = false;
    cleanupProgressBar();
}

// Scheduler node persistente para el update loop.
// Se mantiene vivo durante toda la sesion via retain().
class QuickHubScheduler : public CCNode {
public:
    static QuickHubScheduler* get() {
        static QuickHubScheduler* s_instance = nullptr;
        if (!s_instance) {
            s_instance = new QuickHubScheduler();
            s_instance->init();
            s_instance->retain();
            CCDirector::get()->getScheduler()->scheduleSelector(
                schedule_selector(QuickHubScheduler::onUpdate),
                s_instance, 0.f, false
            );
        }
        return s_instance;
    }

    void onUpdate(float dt) {
        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) return;
        if (!s_hold.ctrlDown) return;
        if (s_hold.radialOpened) return;
        if (s_hold.cancelledByOtherKey) return;

        s_hold.elapsed += dt;

        // Fase 1: dead zone (0 → 0.5s) — no hacer nada visible
        if (s_hold.elapsed < kDeadZone) return;

        // Fase 2: mostrar barra y llenarla (0.5s → 1.5s)
        if (!s_hold.barVisible) {
            createProgressBar();
        }

        float fillProgress = (s_hold.elapsed - kDeadZone) / kFillDuration;
        updateProgressBar(fillProgress);

        // Fase 3: abrir el radial cuando se completa
        if (s_hold.elapsed >= kTotalHold) {
            cleanupProgressBar();
            s_hold.radialOpened = true;
            paimon::quickhub::QuickHubRadial::openRadial();
        }
    }
};

} // anonymous namespace

// API publica para que otras features (volume-scroll) puedan cancelar el hold
// del Ctrl cuando el usuario lo esta usando para Ctrl+Scroll de volumen.
//
// Si el radial todavia no se ha abierto, marcamos el hold como cancelado y
// limpiamos la barra de progreso. Si ya se abrio, no tocamos nada.
namespace paimon::quickhub {
    void notifyVolumeScrollUsed() {
        if (s_hold.ctrlDown && !s_hold.radialOpened) {
            s_hold.cancelledByOtherKey = true;
            cleanupProgressBar();
        }
    }

    void QuickHubManager::abortActiveHold() {
        if (QuickHubRadial::isOpen()) {
            QuickHubRadial::closeRadial();
        }
        resetHold();
    }
}

// Hook 1: updateModifierKeys
//
// Este es el camino principal — se dispara al instante en cualquier cambio de
// estado de Ctrl, en todas las plataformas.
//
// Este hook tambien notifica a volume-scroll para que actualice su estado
// interno de modificadores (antes volume-scroll tenia su propio $modify
// sobre el mismo metodo; consolidamos para no duplicar la cadena cooperativa).

namespace paimon::volscroll {
    // Definida en src/features/volume-scroll/hooks/VolumeScrollHook.cpp
    void onModifierKeysChanged(bool shft, bool ctrl, bool alt, bool cmd);
}

class $modify(QuickHubModifierHook, CCKeyboardDispatcher) {
    void updateModifierKeys(bool shft, bool ctrl, bool alt, bool cmd) {
        CCKeyboardDispatcher::updateModifierKeys(shft, ctrl, alt, cmd);

        // Notificar a volume-scroll antes de la logica de QuickHub.
        paimon::volscroll::onModifierKeysChanged(shft, ctrl, alt, cmd);

        // En macOS, el Cmd se reporta como Ctrl en m_bControlPressed (ver
        // CCKeyboardDispatcher.cpp linea "m_bControlPressed = ctrl || cmd"),
        // pero aqui tenemos las flags raw — usamos solo `ctrl` para que el
        // hold se active igual con Cmd en macOS.
        bool ctrlOrCmd = ctrl || cmd;

        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) {
            if (s_hold.ctrlDown || s_hold.radialOpened) {
                paimon::quickhub::QuickHubManager::abortActiveHold();
            }
            return;
        }

        // Asegurar que el scheduler esta vivo
        QuickHubScheduler::get();

        if (ctrlOrCmd && !s_hold.ctrlDown) {
            // Ctrl recien presionado
            s_hold.ctrlDown = true;
            s_hold.elapsed = 0.f;
            s_hold.barVisible = false;
            s_hold.radialOpened = false;
            s_hold.cancelledByOtherKey = false;
        } else if (!ctrlOrCmd && s_hold.ctrlDown) {
            // Ctrl recien soltado
            if (!s_hold.radialOpened) {
                resetHold();
            } else {
                // El radial esta abierto — el reset se hara cuando el radial
                // se cierre (touch o escape). Solo limpiamos las flags del hold.
                s_hold.ctrlDown = false;
                s_hold.elapsed = 0.f;
            }
        }
    }
};

// Hook 2: dispatchKeyboardMSG (cancelar hold si pulsa otra tecla)
//
// Si el usuario hace Ctrl+S, Ctrl+P, etc. mientras tiene Ctrl presionado, no
// queremos que el radial se abra. Marcamos el hold como cancelado en cuanto
// se dispatcha cualquier otra tecla.

class $modify(QuickHubKeyHook, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        // Solo nos interesan key-down events (no repeat, no key-up)
        if (!paimon::quickhub::QuickHubManager::isHoldCtrlEnabled()) {
            return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
        }

        if (down && !repeat && s_hold.ctrlDown && !s_hold.radialOpened) {
            // Ignorar las propias teclas modificadoras
            bool isModifier =
                key == KEY_Control || key == KEY_LeftControl || key == KEY_RightContol ||
                key == KEY_Shift   || key == KEY_LeftShift   || key == KEY_RightShift   ||
                key == KEY_Alt     || key == KEY_LeftMenu    || key == KEY_RightMenu;

            if (!isModifier) {
                s_hold.cancelledByOtherKey = true;
                cleanupProgressBar();
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};
