#pragma once

namespace paimon::ui::constants {

// PaiConfigLayer (editor de fondos). Todo se mide desde los bordes de la
// pantalla para que aguante 16:9 y 4:3 sin tocar nada mas.
namespace config {
    constexpr float HEADER_Y       = 17.f;  // desde arriba: titulo / back / info
    constexpr float TAB_Y          = 47.f;  // desde arriba: barra de pestanas
    constexpr float TAB_WIDTH      = 118.f;

    constexpr float CONTENT_TOP    = 67.f;  // desde arriba: inicio del contenido
    constexpr float CONTENT_BOTTOM = 42.f;  // desde abajo: fin del contenido
    constexpr float FOOTER_Y       = 20.f;  // botones de la barra inferior

    constexpr float EDGE_PAD       = 6.f;   // margen lateral de las tarjetas
    constexpr float GUTTER         = 6.f;   // separacion entre tarjetas

    constexpr float LIST_BOTTOM    = 12.f;  // desde abajo de la tarjeta: base de la lista
    constexpr float LIST_HINT_Y    = 6.f;   // flecha de "hay mas abajo", bajo la lista

    constexpr float PROFILE_THUMB_SIZE = 70.f;
}

namespace editor {
    constexpr float SNAP_THRESHOLD      = 8.f;
    constexpr unsigned char OVERLAY_ALPHA = 120;

    constexpr float SELECTION_R = 0.39f;
    constexpr float SELECTION_G = 1.00f;
    constexpr float SELECTION_B = 0.39f;
    constexpr float SELECTION_A = 0.80f;

    constexpr float BUTTON_HL_R = 0.30f;
    constexpr float BUTTON_HL_G = 0.50f;
    constexpr float BUTTON_HL_B = 1.00f;
    constexpr float BUTTON_HL_A = 0.47f;

    constexpr float SNAP_GUIDE_R = 0.00f;
    constexpr float SNAP_GUIDE_G = 1.00f;
    constexpr float SNAP_GUIDE_B = 0.50f;
    constexpr float SNAP_GUIDE_A = 0.80f;

    constexpr float SCALE_MIN = 0.3f;
    constexpr float SCALE_MAX = 2.0f;

    constexpr float CONTROLS_PANEL_H = 100.f;
    constexpr float CORNER_RADIUS    = 3.f;
    constexpr int   ARC_SEGMENTS     = 8;

    constexpr int TOUCH_PRIORITY = -500;

    constexpr int Z_CONTROLS_MENU     = 1001;
    constexpr int Z_SELECTION_HL      = 999;
    constexpr int Z_BUTTON_HL         = 998;
}

namespace shared {
    constexpr float FONT_SCALE_TITLE    = 0.55f;
    constexpr float FONT_SCALE_LABEL    = 0.35f;
    constexpr float FONT_SCALE_SMALL    = 0.25f;
    constexpr float FONT_SCALE_VALUE    = 0.30f;

    constexpr float BTN_SCALE_LARGE     = 0.55f;
    constexpr float BTN_SCALE_MEDIUM    = 0.45f;
    constexpr float BTN_SCALE_SMALL     = 0.35f;
    constexpr float BTN_SCALE_TINY      = 0.32f;

    constexpr unsigned char DARK_OVERLAY_ALPHA = 180;
}

} // namespace paimon::ui::constants
