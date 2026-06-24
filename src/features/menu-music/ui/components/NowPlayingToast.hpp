#pragma once

// NowPlayingToast — notificacion flotante que muestra el nombre de la cancion
// actual. Usa una animacion de "circulo -> pill -> circulo" pulida:
//
//   1. Drop-in: baja desde fuera de pantalla como un pequeno circulo.
//   2. Expand: el circulo se estira horizontalmente hasta formar la pill,
//      y el contenido (icono + labels) aparece con fade-in sincronizado.
//   3. Hold: la pill se queda fija `stayFor` segundos.
//   4. Collapse: el contenido desaparece primero con fade-out y la pill
//      se colapsa a circulo.
//   5. Lift-out: el circulo vuelve a subir fuera de pantalla.
//
// Todo el pipeline lo maneja un update loop propio (schedule_selector)
// en lugar de encadenar CCActions porque necesitamos re-dibujar el
// CCDrawNode de la pill cada frame para interpolar su ancho — los
// CCActions nativos solo aplican escalas/opacidades/posicion.

#include <Geode/Geode.hpp>
#include <string>

namespace paimon::menumusic {

class NowPlayingToast : public cocos2d::CCNode {
public:
    // Muestra el toast para el track actualmente activo del player.
    // Solo-uno-a-la-vez: si ya hay un toast en el parent, lo reemplaza.
    static void showForCurrent(cocos2d::CCNode* parent);

    void onExit() override;

protected:
    bool init(const std::string& title, const std::string& subtitle);

    // Animation phases
    enum class Phase {
        Wait,      // delay inicial antes de arrancar (para que el main menu termine de montarse)
        DropIn,    // position Y: hide -> show, pill queda como circulo
        Expand,    // pill se abre, contenido fade-in
        Hold,      // pill full width, espera stayFor
        Collapse,  // contenido fade-out, pill vuelve a circulo
        LiftOut,   // position Y: show -> hide
        Done,      // remueve al final
    };

    void onTick(float dt);
    void redrawPill(float width);
    void setContentOpacity(float opacity01);

    // Easing: cubic. Manuales para no depender del enum CCActionEase.
    static float easeOutCubic(float t);
    static float easeInCubic(float t);
    static float easeInOutCubic(float t);

    cocos2d::CCDrawNode* m_pill = nullptr;
    cocos2d::CCNode* m_contentHolder = nullptr;

    // Geometria de la pill:
    //   * `m_circleWidth` es el ancho cuando la pill es solo un circulo
    //     (equivale a la altura de la pill).
    //   * `m_pillWidth` es el ancho "completo" cuando se ha expandido.
    //     Los dos valores se calculan en init() y no cambian despues.
    float m_circleWidth = 0.f;
    float m_pillWidth = 0.f;
    float m_pillHeight = 0.f;

    // Y de escenario:
    //   * `m_showY` es la posicion visible (centrado cerca del top).
    //   * `m_hideY` es fuera de pantalla por arriba.
    float m_showY = 0.f;
    float m_hideY = 0.f;

    // Tiempo de hold (configurable via setting).
    float m_stayFor = 1.5f;

    // Estado del timer actual.
    Phase m_phase = Phase::DropIn;
    float m_phaseTime = 0.f;
    float m_phaseDuration = 0.f;
};

} // namespace paimon::menumusic
