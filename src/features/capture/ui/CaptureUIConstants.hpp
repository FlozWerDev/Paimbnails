#pragma once

namespace paimon::capture {

namespace preview {
    constexpr float POPUP_WIDTH  = 480.f;
    constexpr float POPUP_HEIGHT = 320.f;

    constexpr float PREVIEW_PAD_X   = 30.f;
    constexpr float PREVIEW_PAD_TOP = 50.f;
    constexpr float PREVIEW_PAD_BOT = 58.f;
    constexpr float PREVIEW_OFFSET_Y = 8.f;
    constexpr float BORDER_MARGIN   = 4.f;

    constexpr unsigned char CLIP_BG_ALPHA = 200;

    constexpr float TOOLBAR_HEIGHT   = 40.f;
    constexpr float TOOLBAR_Y        = 30.f;
    constexpr float TOOLBAR_GAP      = 10.f;
    constexpr float TOOLBAR_EDIT_GAP = 8.f;
    constexpr float TOOLBAR_SEP_GAP  = 18.f;
    constexpr float BTN_TARGET_SIZE  = 28.f;

    constexpr float ZOOM_MAX_BASE  = 4.0f;
    constexpr float ZOOM_MAX_MULT  = 6.0f;
    constexpr float SCROLL_ZOOM_IN  = 1.12f;
    constexpr float SCROLL_ZOOM_OUT = 0.89f;
    constexpr float SMOOTH_SCROLL_ZOOM_SCALE = 0.05f;

    constexpr int TOUCH_PRIORITY = -502;

    constexpr int    CROP_BLACK_THRESHOLD  = 20;
    constexpr float  CROP_BLACK_PERCENTAGE = 0.85f;
    constexpr int    CROP_SAMPLE_STEP      = 4;
    constexpr float  CROP_MIN_RATIO        = 0.30f;
    constexpr float  CROP_MAX_RATIO        = 0.99f;

    constexpr float RECAPTURE_TIMEOUT_SEC = 5.0f;

    // Offscreen size of the live thumbnail shown by the editor popups. 16:9,
    // same framing as the real capture.
    constexpr int MINI_RT_WIDTH  = 480;
    constexpr int MINI_RT_HEIGHT = 270;
}

// Shared chrome for the two editor popups (layer editor / asset browser): a
// header band with the live preview on the left and tools on the right, then a
// full-width list, then the action row.
namespace editor {
    // The popup title sits ~20pt from the top and is ~20pt tall, so the header
    // band starts below that. PREVIEW_W/H keep the 16:9 framing of the capture.
    constexpr float HEADER_TOP_PAD  = 33.f;
    constexpr float SIDE_PAD        = 10.f;
    constexpr float PREVIEW_W       = 128.f;
    constexpr float PREVIEW_H       = 72.f;
    constexpr float TOOLS_GAP       = 12.f;  // preview -> right column
    constexpr float LIST_GAP_BELOW_HEADER = 10.f;
    constexpr float LIST_BOT        = 36.f;

    constexpr unsigned char GROUP_BG_ALPHA     = 34;
    constexpr unsigned char GROUP_ACCENT_ALPHA = 150;
    constexpr float GROUP_ACCENT_WIDTH = 3.f;
    constexpr unsigned char ALT_ROW_ALPHA = 10;

    constexpr float ARROW_X     = 11.f;
    constexpr float ARROW_SCALE = 0.32f;
}

namespace layers {
    constexpr float POPUP_WIDTH  = 400.f;
    constexpr float POPUP_HEIGHT = 300.f;

    constexpr float FILTER_BTN_WIDTH  = 150.f;
    constexpr float FILTER_BTN_HEIGHT = 20.f;

    constexpr float ROW_HEIGHT   = 26.f;
    constexpr float DEPTH_INDENT = 11.f;
    constexpr float CHECK_X_FROM_RIGHT = 17.f;
    constexpr float LABEL_X_BASE = 24.f;

    constexpr float CHECK_SCALE_GROUP = 0.5f;
    constexpr float CHECK_SCALE_LEAF  = 0.44f;

    constexpr float LABEL_SCALE_GROUP   = 0.34f;
    constexpr float LABEL_SCALE_LEAF_D0 = 0.29f;
    constexpr float LABEL_SCALE_LEAF_D2 = 0.26f;
    constexpr float COUNT_SCALE         = 0.24f;

    constexpr float OPTION_HEIGHT = 26.f;
}

namespace assets {
    constexpr float POPUP_WIDTH  = 440.f;
    constexpr float POPUP_HEIGHT = 300.f;

    constexpr float ROW_HEIGHT   = 30.f;
    constexpr float SPRITE_SIZE  = 24.f;
    constexpr float SPRITE_X     = 40.f;
    constexpr float LABEL_X      = 60.f;
    constexpr float COUNT_X_FROM_RIGHT  = 84.f;
    constexpr float SOLO_X_FROM_RIGHT   = 50.f;
    constexpr float CHECK_X_FROM_RIGHT  = 17.f;

    constexpr float CHECK_SCALE        = 0.44f;
    constexpr float CHECK_SCALE_HEADER = 0.5f;

    constexpr float LABEL_SCALE_HEADER = 0.34f;
    constexpr float LABEL_SCALE_ROW    = 0.30f;
    constexpr float COUNT_SCALE        = 0.25f;

    constexpr float SEARCH_WIDTH  = 150.f;
    constexpr float SEARCH_SCALE  = 0.6f;
}

} // namespace paimon::capture
