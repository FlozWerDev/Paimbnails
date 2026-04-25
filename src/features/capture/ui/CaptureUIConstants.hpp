#pragma once

// CaptureUIConstants.hpp — Named constants for the capture UI system.
// Replaces scattered magic numbers across CapturePreviewPopup,
// CaptureLayerEditorPopup, and related files.

namespace paimon::capture {

// ─── CapturePreviewPopup ─────────────────────────────────────────

namespace preview {
    // Popup dimensions
    constexpr float POPUP_WIDTH  = 480.f;
    constexpr float POPUP_HEIGHT = 320.f;

    // Preview area
    constexpr float PREVIEW_PAD_X   = 30.f;  // horizontal inset from popup edges
    constexpr float PREVIEW_PAD_TOP = 50.f;  // top inset (room for title)
    constexpr float PREVIEW_PAD_BOT = 58.f;  // bottom inset (room for toolbar)
    constexpr float PREVIEW_OFFSET_Y = 8.f;  // vertical nudge toward center
    constexpr float BORDER_MARGIN   = 4.f;   // decorative border overshoot

    // Dark background inside clipping
    constexpr unsigned char CLIP_BG_ALPHA = 200;

    // Toolbar
    constexpr float TOOLBAR_HEIGHT   = 40.f;
    constexpr float TOOLBAR_Y        = 30.f;  // center Y of button row
    constexpr float TOOLBAR_GAP      = 10.f;  // gap between buttons
    constexpr float TOOLBAR_EDIT_GAP = 8.f;   // gap within edit control group
    constexpr float TOOLBAR_SEP_GAP  = 18.f;  // gap between edit group and action group
    constexpr float BTN_TARGET_SIZE  = 28.f;  // normalized button icon size

    // Zoom / pan
    constexpr float ZOOM_MAX_BASE  = 4.0f;
    constexpr float ZOOM_MAX_MULT  = 6.0f;
    constexpr float SCROLL_ZOOM_IN  = 1.12f;
    constexpr float SCROLL_ZOOM_OUT = 0.89f;

    // Touch priority
    constexpr int TOUCH_PRIORITY = -502;

    // Crop detection
    constexpr int    CROP_BLACK_THRESHOLD  = 20;
    constexpr float  CROP_BLACK_PERCENTAGE = 0.85f;
    constexpr int    CROP_SAMPLE_STEP      = 4;
    constexpr float  CROP_MIN_RATIO        = 0.30f;
    constexpr float  CROP_MAX_RATIO        = 0.99f;

    // Recapture timeout
    constexpr float RECAPTURE_TIMEOUT_SEC = 5.0f;
}

// ─── CaptureLayerEditorPopup ─────────────────────────────────────

namespace layers {
    // Popup dimensions
    constexpr float POPUP_WIDTH  = 370.f;
    constexpr float POPUP_HEIGHT = 295.f;

    // Mini preview
    constexpr float MINI_PREVIEW_W = 180.f;
    constexpr float MINI_PREVIEW_H = 100.f;
    constexpr float MINI_PREVIEW_TOP_PAD = 26.f;

    // Render texture for mini preview
    constexpr int RT_WIDTH  = 480;
    constexpr int RT_HEIGHT = 270;

    // Filter
    constexpr float FILTER_GAP_BELOW_PREVIEW = 12.f;
    constexpr float FILTER_BTN_WIDTH = 140.f;
    constexpr float FILTER_BTN_HEIGHT = 18.f;

    // List
    constexpr float LIST_PAD_X   = 8.f;
    constexpr float LIST_BOT     = 38.f;
    constexpr float LIST_GAP_BELOW_FILTER = 12.f;
    constexpr float ROW_HEIGHT   = 28.f;
    constexpr float DEPTH_INDENT = 12.f;
    constexpr float CHECK_X_BASE = 18.f;
    constexpr float LABEL_X_BASE = 38.f;

    // Checkbox scales
    constexpr float CHECK_SCALE_GROUP = 0.52f;
    constexpr float CHECK_SCALE_LEAF  = 0.45f;

    // Label scales
    constexpr float LABEL_SCALE_GROUP  = 0.35f;
    constexpr float LABEL_SCALE_LEAF_D0 = 0.29f;
    constexpr float LABEL_SCALE_LEAF_D2 = 0.26f;

    // Group row colors
    constexpr unsigned char GROUP_BG_ALPHA = 30;
    constexpr unsigned char GROUP_ACCENT_ALPHA = 140;
    constexpr float GROUP_ACCENT_WIDTH = 3.f;

    // Alternating row tint
    constexpr unsigned char ALT_ROW_ALPHA = 10;

    // Filter dropdown option height
    constexpr float OPTION_HEIGHT = 26.f;
}

// ─── CaptureAssetBrowserPopup ────────────────────────────────────

namespace assets {
    // Popup dimensions
    constexpr float POPUP_WIDTH  = 430.f;
    constexpr float POPUP_HEIGHT = 315.f;

    // Mini preview
    constexpr float MINI_PREVIEW_W = 180.f;
    constexpr float MINI_PREVIEW_H = 100.f;
    constexpr float MINI_PREVIEW_TOP_PAD = 26.f;

    // Render texture for mini preview
    constexpr int RT_WIDTH  = 480;
    constexpr int RT_HEIGHT = 270;

    // List
    constexpr float LIST_PAD_X   = 8.f;
    constexpr float LIST_BOT     = 38.f;
    constexpr float LIST_GAP_BELOW_PREVIEW = 12.f;
    constexpr float ROW_HEIGHT   = 36.f;
    constexpr float PREVIEW_SIZE = 28.f;
    constexpr float PREVIEW_X    = 24.f;
    constexpr float LABEL_X      = 56.f;
    constexpr float COUNT_X_OFF  = -40.f; // offset from right edge

    // Checkbox / toggle
    constexpr float CHECK_SCALE  = 0.45f;
    constexpr float CHECK_X_OFF  = -16.f; // offset from right edge

    // Label scales
    constexpr float LABEL_SCALE_HEADER = 0.35f;
    constexpr float LABEL_SCALE_ROW    = 0.30f;
    constexpr float COUNT_SCALE        = 0.26f;

    // Header row colors
    constexpr unsigned char HEADER_BG_ALPHA    = 30;
    constexpr unsigned char HEADER_ACCENT_ALPHA = 140;
    constexpr float HEADER_ACCENT_WIDTH = 3.f;

    // Alternating row tint
    constexpr unsigned char ALT_ROW_ALPHA = 10;

    // Viewport buffer for object detection (20% extra each side)
    constexpr float VIEWPORT_BUFFER = 0.2f;
}

} // namespace paimon::capture
