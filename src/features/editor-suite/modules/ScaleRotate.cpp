// Numeric inputs for Scale XY / X / Y + rotation, Shift-snap while dragging.
// Patterns from BetterEdit ImprovedScaleAndRotate (inputs + snap + lock pos).

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJRotationControl.hpp>
#include <Geode/binding/GJScaleControl.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/modify/GJRotationControl.hpp>
#include <Geode/modify/GJScaleControl.hpp>
#include <Geode/ui/TextInput.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

bool on() { return moduleEnabled("editor-mod-scale-rotate"); }

float scaleSnap() {
    return static_cast<float>(moduleSetting<double>("editor-mod-scale-snap", 0.25));
}
float rotSnap() {
    return static_cast<float>(moduleSetting<double>("editor-mod-rotate-snap", 15.0));
}
bool shiftHeld() {
    auto* k = CCKeyboardDispatcher::get();
    return k && k->getShiftKeyPressed();
}
float snapf(float v, float step) {
    if (!std::isfinite(v) || !std::isfinite(step) || step <= 0.f) return v;
    return std::round(v / step) * step;
}

TextInput* makeScaleInput(char const* id, float w = 44.f) {
    auto* input = TextInput::create(w, "1.00");
    input->setScale(0.7f);
    input->setID(id);
    input->setCommonFilter(CommonFilter::Float);
    return input;
}

} // namespace

class $modify(PaimonScaleControl, GJScaleControl) {
    struct Fields {
        Ref<TextInput> inputXY;
        Ref<TextInput> inputX;
        Ref<TextInput> inputY;
    };

    void wireInput(TextInput* input, std::function<void(float)> apply) {
        if (!input) return;
        input->setCallback([this, apply = std::move(apply)](std::string const& s) {
            if (!on() || !m_delegate) return;
            if (auto v = numFromString<float>(s)) {
                float sc = v.unwrap();
                if (!std::isfinite(sc)) return;
                sc = std::clamp(sc, m_lowerBound, m_upperBound);
                m_delegate->scaleChangeBegin();
                apply(sc);
                m_delegate->scaleChangeEnded();
            }
        });
    }

    $override
    bool init() {
        if (!GJScaleControl::init()) return false;
        if (!on()) return true;

        auto* inXY = makeScaleInput("paimbnails/scale-input-xy", 48.f);
        if (m_scaleLabel) {
            inXY->setPosition(m_scaleLabel->getPosition() + ccp(52.f, 0.f));
        } else {
            inXY->setPosition({40.f, 0.f});
        }
        wireInput(inXY, [this](float sc) {
            m_delegate->scaleXYChanged(sc, sc, m_scaleLocked);
            if (m_sliderXY) m_sliderXY->setValue(this->valueFromScale(sc));
        });
        this->addChild(inXY, 10);
        m_fields->inputXY = inXY;

        auto* inX = makeScaleInput("paimbnails/scale-input-x");
        if (m_scaleXLabel) {
            inX->setPosition(m_scaleXLabel->getPosition() + ccp(48.f, 0.f));
        } else {
            inX->setPosition({40.f, 28.f});
        }
        wireInput(inX, [this](float sc) {
            m_delegate->scaleXChanged(sc, m_scaleLocked);
            if (m_sliderX) m_sliderX->setValue(this->valueFromScale(sc));
        });
        this->addChild(inX, 10);
        m_fields->inputX = inX;

        auto* inY = makeScaleInput("paimbnails/scale-input-y");
        if (m_scaleYLabel) {
            inY->setPosition(m_scaleYLabel->getPosition() + ccp(48.f, 0.f));
        } else {
            inY->setPosition({40.f, -28.f});
        }
        wireInput(inY, [this](float sc) {
            m_delegate->scaleYChanged(sc, m_scaleLocked);
            if (m_sliderY) m_sliderY->setValue(this->valueFromScale(sc));
        });
        this->addChild(inY, 10);
        m_fields->inputY = inY;

        return true;
    }

    $override
    void updateLabelXY(float value) {
        GJScaleControl::updateLabelXY(value);
        if (!on() || !m_fields->inputXY) return;
        float sc = this->scaleFromValue(
            m_sliderXY ? m_sliderXY->getThumb()->getValue() : value
        );
        m_fields->inputXY->setString(fmt::format("{:.3f}", sc));
    }

    $override
    void updateLabelX(float value) {
        GJScaleControl::updateLabelX(value);
        if (!on() || !m_fields->inputX) return;
        float sc = this->scaleFromValue(
            m_sliderX ? m_sliderX->getThumb()->getValue() : value
        );
        m_fields->inputX->setString(fmt::format("{:.3f}", sc));
    }

    $override
    void updateLabelY(float value) {
        GJScaleControl::updateLabelY(value);
        if (!on() || !m_fields->inputY) return;
        float sc = this->scaleFromValue(
            m_sliderY ? m_sliderY->getThumb()->getValue() : value
        );
        m_fields->inputY->setString(fmt::format("{:.3f}", sc));
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (!on() || !shiftHeld()) {
            return GJScaleControl::ccTouchMoved(touch, event);
        }
        GJScaleControl::ccTouchMoved(touch, event);
        if (!m_delegate) return;
        float step = scaleSnap();

        auto snapSlider = [&](Slider* slider, auto apply) {
            if (!slider) return;
            float sc = this->scaleFromValue(slider->getThumb()->getValue());
            sc = snapf(sc, step);
            apply(sc);
            slider->setValue(this->valueFromScale(sc));
        };

        snapSlider(m_sliderXY, [&](float sc) {
            m_delegate->scaleXYChanged(sc, sc, m_scaleLocked);
            if (m_fields->inputXY) m_fields->inputXY->setString(fmt::format("{:.3f}", sc));
        });
        snapSlider(m_sliderX, [&](float sc) {
            m_delegate->scaleXChanged(sc, m_scaleLocked);
            if (m_fields->inputX) m_fields->inputX->setString(fmt::format("{:.3f}", sc));
        });
        snapSlider(m_sliderY, [&](float sc) {
            m_delegate->scaleYChanged(sc, m_scaleLocked);
            if (m_fields->inputY) m_fields->inputY->setString(fmt::format("{:.3f}", sc));
        });
    }
};

class $modify(PaimonRotationControl, GJRotationControl) {
    struct Fields {
        Ref<TextInput> input;
        // BetterEdit-style: lock selection center while rotating (optional).
        cocos2d::CCPoint lockCenter{0.f, 0.f};
        bool locking = false;
    };

    bool lockPos() {
        return moduleSetting<bool>("editor-mod-rotate-lock-pos", false);
    }

    void captureLockCenter() {
        m_fields->locking = false;
        if (!lockPos()) return;
        auto* ui = EditorUI::get();
        if (!ui) return;
        auto sel = getSelectedObjects(ui);
        if (sel.empty()) return;
        float sx = 0.f, sy = 0.f;
        for (auto* o : sel) {
            auto p = o->getPosition();
            sx += p.x;
            sy += p.y;
        }
        float n = static_cast<float>(sel.size());
        m_fields->lockCenter = ccp(sx / n, sy / n);
        m_fields->locking = true;
    }

    void applyLockCenter() {
        if (!m_fields->locking || !lockPos()) return;
        auto* ui = EditorUI::get();
        if (!ui) return;
        auto sel = getSelectedObjects(ui);
        if (sel.empty()) return;
        float sx = 0.f, sy = 0.f;
        for (auto* o : sel) {
            auto p = o->getPosition();
            sx += p.x;
            sy += p.y;
        }
        float n = static_cast<float>(sel.size());
        CCPoint cur{sx / n, sy / n};
        auto d = m_fields->lockCenter - cur;
        if (!std::isfinite(d.x) || !std::isfinite(d.y)
            || (std::abs(d.x) < 0.0001f && std::abs(d.y) < 0.0001f)) {
            return;
        }
        for (auto* o : sel) {
            if (!o) continue;
            o->setPosition(o->getPosition() + d);
            ui->m_editorLayer->updateObjectSection(o);
        }
    }

    void applyAngle(float ang) {
        m_currentRotation = ang;
        if (m_controlSprite) {
            float rad = CC_DEGREES_TO_RADIANS(90.f - ang);
            m_controlPosition = ccp(std::cos(rad) * 60.f, std::sin(rad) * 60.f);
            m_controlSprite->setPosition(m_controlPosition);
        }
        if (m_fields->input) {
            m_fields->input->setString(fmt::format("{:.2f}", ang));
        }
    }

    $override
    bool init() {
        if (!GJRotationControl::init()) return false;
        if (!on()) return true;

        auto* input = TextInput::create(50.f, "0");
        input->setScale(0.75f);
        input->setID("paimbnails/rotate-input-angle");
        input->setCommonFilter(CommonFilter::Float);
        input->setPosition({100.f, 0.f});
        input->setCallback([this](std::string const& s) {
            if (!on() || !m_delegate) return;
            if (auto v = numFromString<float>(s)) {
                float ang = v.unwrap();
                if (!std::isfinite(ang)) return;
                m_delegate->angleChangeBegin();
                m_delegate->angleChanged(ang);
                m_delegate->angleChangeEnded();
                applyAngle(ang);
            }
        });
        this->addChild(input, 10);
        m_fields->input = input;
        return true;
    }

    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        auto const accepted = GJRotationControl::ccTouchBegan(touch, event);
        if (accepted) captureLockCenter();
        return accepted;
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (!on() || !shiftHeld()) {
            GJRotationControl::ccTouchMoved(touch, event);
            applyLockCenter();
            return;
        }
        auto* del = m_delegate;
        m_delegate = nullptr;
        GJRotationControl::ccTouchMoved(touch, event);
        m_delegate = del;
        if (!del) return;

        float ang = snapf(m_currentRotation, rotSnap());
        applyAngle(ang);
        del->angleChanged(ang);
        applyLockCenter();
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        GJRotationControl::ccTouchEnded(touch, event);
        applyLockCenter();
        m_fields->locking = false;
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        GJRotationControl::ccTouchCancelled(touch, event);
        applyLockCenter();
        m_fields->locking = false;
    }
};
