#pragma once

#include <cstdint>

// Shared UI and cache constants.
namespace PaimonConstants {
    constexpr float BORDER_THICKNESS = 6.0f;
    constexpr float MIN_THUMB_WIDTH_FACTOR = 0.2f;
    constexpr float MAX_THUMB_WIDTH_FACTOR = 0.95f;
    
    // Fallback spinner position when LevelCell has no background layer
    constexpr float LEVELCELL_SPINNER_FALLBACK_X = 280.0f;
    constexpr float LEVELCELL_SPINNER_FALLBACK_Y = 30.0f;

    constexpr uint8_t DARK_OVERLAY_ALPHA = 128;
    constexpr uint8_t UI_BLACK_THRESHOLD = 10;
    constexpr uint8_t UI_WHITE_THRESHOLD = 245;

    constexpr const char* THUMBNAIL_CDN_URL = "https://api.flozwer.org/";
}
