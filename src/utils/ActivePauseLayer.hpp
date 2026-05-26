#pragma once

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <atomic>

namespace paimon {
    // ── Active PauseLayer registry ────────────────────────────────────
    //
    // Anteriormente esta variable era un `geode::WeakRef<PauseLayer>` global
    // heap-allocado. Ese diseño causaba un EXCEPTION_ACCESS_VIOLATION en
    // `WeakRefPool::check` (cocos.cpp:285) durante `PauseLayer::customSetup`
    // — el operator= del WeakRef llamaba a `WeakRefController::swap`, que a
    // su vez tocaba el WeakRefPool con el controller del PauseLayer anterior.
    // Si la sesión previa se cerró sin pasar por `clearActivePauseLayer`
    // (scene transitions abruptas, otros mods que destruyen el PauseLayer
    // sin invocar onExit del modificador, recycling del controller en el
    // pool), `check()` desreferenciaba un puntero colgante.
    //
    // El WeakRef era innecesario: este puntero solo se usa para
    // **comparación de identidad** dentro de `PaimonPauseZoomVisitFilter::visit`
    // y para logs (uintptr_t casts). Nunca se desreferencia para acceder a
    // miembros del PauseLayer. Por eso podemos usar un raw atomic pointer:
    //
    //   • setActivePauseLayer(this) corre en customSetup → objeto vivo.
    //   • clearActivePauseLayer(this) corre en onExit → antes del destructor.
    //   • getActivePauseLayer() solo se compara con `this` dentro de visit(),
    //     y visit() solo corre mientras el layer está en el scene tree, que
    //     a su vez exige que esté vivo.
    //
    // Para casos en los que clearActivePauseLayer no se ejecuta (otro mod
    // destruye el PauseLayer sin pasar por onExit, p.ej. una recarga de
    // escena abrupta), el peor caso es un raw pointer colgante. Eso es OK
    // porque la siguiente llamada a setActivePauseLayer en un PauseLayer
    // nuevo lo sobreescribe atómicamente, y entre destrucción y nuevo
    // customSetup nadie llama a visit() sobre el PauseLayer destruido.
    inline std::atomic<PauseLayer*>& activePauseLayerAtomic() {
        static std::atomic<PauseLayer*> s_activePauseLayer{nullptr};
        return s_activePauseLayer;
    }

    inline void setActivePauseLayer(PauseLayer* layer) {
        activePauseLayerAtomic().store(layer, std::memory_order_release);
    }

    inline PauseLayer* getActivePauseLayer() {
        return activePauseLayerAtomic().load(std::memory_order_acquire);
    }

    inline void clearActivePauseLayer(PauseLayer* layer) {
        // Solo limpiamos si el PauseLayer activo es exactamente este. Sin
        // esta guarda, si dos PauseLayers se solapan (ej. transición mientras
        // otro mod inserta uno nuevo), podríamos limpiar al "nuevo" cuando
        // realmente queríamos limpiar al viejo.
        auto& slot = activePauseLayerAtomic();
        PauseLayer* expected = layer;
        slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }

    // Notifica al PauseZoomManager (definido en PlayLayer.cpp) que el
    // PauseLayer esta a punto de cerrarse. Sin esto, el ticker del manager
    // seguiria llamando showLayer()/setVisible() sobre el PauseLayer durante
    // su animacion de salida y causa que el menu se "pegue" o crashee al
    // despausar rapido. Implementacion en PlayLayer.cpp.
    void notifyPauseClosing();

    // Flag global de "captura en curso" usado para coordinar visibilidad
    // del PauseLayer entre PauseLayer::onScreenshot (que hace setVisible(false))
    // y PauseZoomManager::update() (que cada frame ajusta visibilidad por zoom).
    //
    // Sin este flag, PauseZoomManager::update() podria restaurar visibilidad
    // del PauseLayer durante los ~0.05s entre setVisible(false) y la captura
    // real en swapBuffers, haciendo que el menu aparezca en la screenshot.
    inline std::atomic<bool>& captureInProgressFlag() {
        static std::atomic<bool> s_inProgress{false};
        return s_inProgress;
    }

    inline bool isCaptureInProgress() {
        return captureInProgressFlag().load(std::memory_order_acquire);
    }

    inline void setCaptureInProgress(bool inProgress) {
        captureInProgressFlag().store(inProgress, std::memory_order_release);
    }

    // ── Flag de "PauseLayer oculto por zoom" ─────────────────────────
    //
    // El PauseZoomManager hace setVisible(false) cuando el usuario hace
    // zoom con scroll/keybind, pero los logs (debug-347aef.log) muestran
    // que m_bVisible vuelve a true entre frames — algun mecanismo interno
    // de CCBlockLayer (showLayer/enterAnimFinished) o de otro mod restaura
    // visibilidad. Setear false 60+ veces por segundo no funciona si algo
    // mas lo pone true cada frame.
    //
    // Solucion: este flag se consulta en PauseLayer::visit() (override en
    // PauseLayer.cpp). Cuando esta activo, visit() retorna early y la capa
    // no se renderiza, sin importar m_bVisible. Es imposible de evadir.
    //
    // El flag se setea en hidePauseMenu()/restorePauseMenuVisible() del
    // PauseZoomManager y se resetea en onResume(), notifyPauseClosing(),
    // y onExit() del PauseLayer para no leakear estado entre escenas.
    inline std::atomic<bool>& pauseZoomHiddenFlag() {
        static std::atomic<bool> s_zoomHidden{false};
        return s_zoomHidden;
    }

    inline bool isPauseZoomHidden() {
        return pauseZoomHiddenFlag().load(std::memory_order_acquire);
    }

    inline void setPauseZoomHidden(bool hidden) {
        pauseZoomHiddenFlag().store(hidden, std::memory_order_release);
    }

    // ── Detección de PauseLayer en escena ─────────────────────────────
    //
    // El registro `activePauseLayerAtomic()` solo se actualiza durante
    // `customSetup` y `onExit`. Si Esc se presiona en el frame N y T
    // (capture-keybind) en el mismo frame N, el listener del keybind se
    // ejecuta antes de que `customSetup` corra, así que `getActivePauseLayer`
    // devuelve nullptr aunque GD ya haya marcado m_isPaused = true.
    //
    // Esta función es la verdad sobre el terreno: barre la escena buscando
    // un PauseLayer real. Es O(N) donde N = hijos de la escena (~20),
    // mucho más fiable que confiar solo en el flag.
    inline bool hasPauseLayerInScene() {
        auto* director = cocos2d::CCDirector::sharedDirector();
        if (!director) return false;
        auto* scene = director->getRunningScene();
        if (!scene) return false;
        auto* children = scene->getChildren();
        if (!children) return false;
        for (auto* obj : geode::cocos::CCArrayExt<cocos2d::CCObject*>(children)) {
            if (geode::cast::typeinfo_cast<PauseLayer*>(obj)) {
                return true;
            }
        }
        return false;
    }
}
