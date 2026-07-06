#pragma once

namespace paimon::ui::constants {

namespace config {
    constexpr float CONTENT_TOP_OFFSET  = 64.f;
    constexpr float CONTENT_BOT         = 42.f;

    constexpr float TAB_Y_OFFSET        = 46.f;
    constexpr float TAB_SEP_BELOW       = 14.f;
    constexpr float TAB_WIDTH           = 120.f;
    constexpr float APPLY_BTN_Y         = 20.f;

    constexpr float SIDEBAR_WIDTH       = 95.f;
    constexpr float SIDEBAR_PAD         = 8.f;
    constexpr float SIDEBAR_SEP_X       = 12.f;

    constexpr float CTRL_PANEL_H        = 80.f;
    constexpr float CTRL_PANEL_TOP_PAD  = 2.f;
    constexpr float CTRL_ROW_SPACING    = 22.f;
    constexpr float CTRL_ROW3_OFFSET    = 18.f;
    constexpr float PREVIEW_GAP         = 12.f;

    constexpr float INPUT_BG_WIDTH      = 65.f;
    constexpr float INPUT_BG_HEIGHT     = 20.f;

    constexpr float PROFILE_PANEL_W     = 420.f;
    constexpr float PROFILE_PANEL_H     = 150.f;
    constexpr float PROFILE_THUMB_SIZE  = 70.f;
    constexpr float PROFILE_BTN_SPACING = 30.f;

    constexpr float EXTRAS_PANEL_W      = 320.f;
    constexpr float EXTRAS_PANEL_H      = 210.f;
    constexpr float EXTRAS_BTN_GAP      = 40.f;
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
