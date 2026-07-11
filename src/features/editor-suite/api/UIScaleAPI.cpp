#include "UIScaleAPI.hpp"
#include "Events.hpp"
#include "../EditorModule.hpp"

namespace paimon::editor::ui_scale {

float currentScale() {
    if (!moduleEnabled("editor-mod-ui-scale")) return 1.f;
    auto s = static_cast<float>(moduleSetting<double>("editor-mod-ui-scale-factor", 0.9));
    if (s >= 0.99f) return 1.f;
    if (s < 0.4f) s = 0.4f;
    if (s > 1.2f) s = 1.2f;
    return s;
}

void notifyScaleChanged(float scale, bool scaleToolbars) {
    EditorUIScaleEvent().send(scale, scaleToolbars);
}

} // namespace paimon::editor::ui_scale
