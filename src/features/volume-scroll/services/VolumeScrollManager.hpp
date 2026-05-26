#pragma once

#include <Geode/Geode.hpp>

namespace paimon::volscroll {

// Tipo de volumen que controla el overlay actualmente
enum class VolumeKind {
    Music,  // Ctrl + scroll → m_musicVolume / setBackgroundMusicVolume
    SFX     // Shift + scroll → m_sfxVolume   / setEffectsVolume
};

// ────────────────────────────────────────────────────────────────────────
// VolumeScrollManager
//
// Singleton que maneja:
//   1) Overlay UI (esquina inferior-derecha) con animación slide-in / slide-out.
//   2) Lectura/escritura del volumen actual de FMODAudioEngine.
//   3) Auto-hide tras un periodo de inactividad.
//   4) Estado de "scroll en uso" para cancelar el hold de Ctrl del Quick Hub.
//
// El nodo se attacha al running scene cuando hace falta y se desattacha al
// terminar la animacion de salida. El ticker (en VolumeScrollHook.cpp) lo
// alimenta cada frame mediante update(dt).
// ────────────────────────────────────────────────────────────────────────

class VolumeScrollManager {
public:
    static VolumeScrollManager& get();

    // ── Lifecycle ──────────────────────────────────────────────────
    void init();
    void update(float dt);
    void onSceneChange();          // llamado cuando cambia el running scene
    void releaseSharedResources(); // limpieza al salir del juego

    // ── API pública (la llama VolumeScrollHook) ────────────────────
    // Aplica delta al volumen del tipo indicado y muestra el overlay.
    // delta: tipicamente +/-0.05 (5%) por click de scroll wheel.
    // Devuelve true si la accion se aplico (true => "consumir" el scroll
    // para que el resto del juego no lo procese).
    bool onScroll(VolumeKind kind, float delta);

    // Indica si hubo un scroll de volumen en los últimos N ms — usado por
    // QuickHubKeybind para no abrir el radial cuando el user esta usando
    // Ctrl+Scroll para subir/bajar volumen.
    bool wasRecentlyUsed(float withinSeconds = 0.35f) const;

    // Estado actual visible (para tests/debug)
    bool isOverlayVisible() const { return m_state != State::Hidden; }

private:
    VolumeScrollManager() = default;

    // Estados de la animación del overlay
    //
    // Flujo completo:
    //   Hidden → SlidingIn (chip MUS/SFX sube desde abajo)
    //          → Expanding (el chip se ensancha y aparecen %, barra, etc.)
    //          → Visible   (totalmente expandido, esperando autohide)
    //          → Collapsing (los extras se desvanecen, el chip se contrae)
    //          → SlidingOut (el chip baja y desaparece)
    //          → Hidden
    enum class State {
        Hidden,
        SlidingIn,
        Expanding,
        Visible,
        Collapsing,
        SlidingOut
    };

    void ensureOverlayBuilt();
    void attachToRunningScene();
    void detachFromScene();
    void rebuildContent();         // re-render del icono + barra + label
    void resetAutoHideTimer();
    void startSlideOut();
    void redrawPill();             // re-dibuja el rounded rect (pildora) con el ancho actual
    void redrawBar();              // re-dibuja la barra con esquinas redondeadas
    void applyExpandProgress();    // posiciona/opacidad de hijos segun m_expandProgress

    // Lee/escribe los volúmenes desde FMODAudioEngine. Clamp a [0, 1].
    float readVolume(VolumeKind kind) const;
    void  writeVolume(VolumeKind kind, float value);

    // ── State ──────────────────────────────────────────────────────
    State m_state = State::Hidden;
    VolumeKind m_currentKind = VolumeKind::Music;
    float m_animProgress = 0.f;    // 0 = oculto debajo, 1 = visible (slide)
    float m_expandProgress = 0.f;  // 0 = chip compacto, 1 = expandido
    float m_visibleTimer = 0.f;    // tiempo restante en estado Visible
    float m_lastUseClock = -100.f; // segundos desde init() del último scroll
    float m_clock = 0.f;
    float m_displayedVolume = 0.f; // valor que muestra la barra (lerp suave)
    float m_targetVolume = 0.f;    // valor real (lo que queremos mostrar)

    // ── UI ─────────────────────────────────────────────────────────
    geode::Ref<cocos2d::CCLayerRGBA> m_overlay;     // contenedor con cascade opacity
    geode::Ref<cocos2d::CCNode> m_pillNode;         // PaimonDrawNode de la pildora (fondo)
    geode::Ref<cocos2d::CCLabelBMFont> m_iconLabel; // chip "MUS" / "SFX" (siempre visible cuando overlay visible)
    geode::Ref<cocos2d::CCLabelBMFont> m_kindLabel; // "Music" / "SFX" (solo visible expandido)
    geode::Ref<cocos2d::CCLabelBMFont> m_label;     // texto "75%" (solo visible expandido)
    geode::Ref<cocos2d::CCNodeRGBA> m_barBg;        // PaimonDrawNode fondo barra (solo expandido)
    geode::Ref<cocos2d::CCNodeRGBA> m_barFill;      // PaimonDrawNode fill barra  (solo expandido)
    cocos2d::CCScene* m_attachedScene = nullptr;    // weak (Cocos retiene parent)
};

} // namespace paimon::volscroll

// Funciones helper para inicializar el ticker (registrado en
// CCDirector::sharedDirector()->getScheduler()). Se llaman desde MenuLayer.cpp
// (init) y desde RuntimeLifecycle.cpp / $on_game(Exiting) (shutdown).
void initVolumeScrollTicker();
void shutdownVolumeScrollTicker();
