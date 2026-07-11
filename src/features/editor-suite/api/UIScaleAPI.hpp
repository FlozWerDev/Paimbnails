#pragma once

// UI scale query + broadcast (inspired by Tinker ui_scaling API).

namespace paimon::editor::ui_scale {

// Current effective scale (1.0 if module off or factor ~1).
float currentScale();

// Notify listeners (also fired by the UIScaling module after apply).
void notifyScaleChanged(float scale, bool scaleToolbars = true);

} // namespace paimon::editor::ui_scale
