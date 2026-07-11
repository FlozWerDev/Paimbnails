// Live color swatch on ConfigureHSVWidget.

#include "../EditorModule.hpp"

#include <Geode/binding/ConfigureHSVWidget.hpp>
#include <Geode/modify/ConfigureHSVWidget.hpp>
#include <cmath>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-hsv-preview"); }

ccColor3B hsvToRgb(float h, float s, float v) {
    if (s > 1.f) s /= 100.f;
    if (v > 1.f) v /= 100.f;
    s = std::clamp(s, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);
    while (h < 0) h += 360.f;
    while (h >= 360.f) h -= 360.f;
    float c = v * s;
    float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return {
        static_cast<GLubyte>(std::clamp((r + m) * 255.f, 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp((g + m) * 255.f, 0.f, 255.f)),
        static_cast<GLubyte>(std::clamp((b + m) * 255.f, 0.f, 255.f)),
    };
}
}

class $modify(PaimonHSVPreview, ConfigureHSVWidget) {
    struct Fields {
        CCLayerColor* swatch = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ConfigureHSVWidget::init");
    }

    $override
    bool init(ccHSVValue hsv, bool noBackground, bool addInputs) {
        if (!ConfigureHSVWidget::init(hsv, noBackground, addInputs)) return false;
        if (!on()) return true;

        auto* sw = CCLayerColor::create({255, 255, 255, 255}, 28.f, 28.f);
        sw->setID("paimbnails/hsv-swatch");
        sw->setPosition({-36.f, 0.f});
        this->addChild(sw, 20);
        m_fields->swatch = sw;
        sw->setColor(hsvToRgb(hsv.h, hsv.s, hsv.v));
        return true;
    }
};
