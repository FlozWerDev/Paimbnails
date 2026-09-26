// After zilko's "Icon Gradients" (independent implementation; idea credit zilko144, unlicensed).

#include "ColorPicker.hpp"
#include "../GradientUtils.hpp"

using namespace geode::prelude;
using namespace paimon::icon_gradients;

namespace {

template <typename Widget>
void hidePickerWidget(CCControlColourPicker* picker, int index) {
    if (Widget* node = picker->getChildByType<Widget>(index))
        node->setVisible(false);
}

} // namespace

ColorPicker* ColorPicker::create() {
    auto ret = new ColorPicker();

    if (!ret->init()) {
        delete ret;
        return nullptr;
    }

    ret->autorelease();
    return ret;
}

bool ColorPicker::init() {
    m_picker = CCControlColourPicker::colourPicker();
    if (!m_picker) return false;

    addChild(m_picker);
    setContentSize(m_picker->getContentSize());

    if (!Loader::get()->isModLoaded("flow.betterpicker")) return true;

    hidePickerWidget<CCMenu>(m_picker, 0);
    for (int i = 0; i < 3; i++) {
        hidePickerWidget<TextInput>(m_picker, i);
        hidePickerWidget<CCLabelBMFont>(m_picker, i);
    }

    return true;
}

void ColorPicker::setDelegate(ColorPickerDelegate* delegate) {
    m_picker->setDelegate(delegate);
}

void ColorPicker::setColor(const ccColor3B& color) {
    m_picker->setColorValue(color);

    // Grayscale has no hue to speak of; without forcing it to zero the
    // hue dragger keeps its stale angle and the picker looks broken.
    if (color.r != color.g || color.r != color.b) return;

    m_picker->m_hsv.h = 0.f;
    if (m_picker->m_huePicker) {
        m_picker->m_huePicker->setHue(0.f);
    }
    if (m_picker->m_colourPicker) {
        m_picker->m_colourPicker->updateWithHSV(m_picker->m_hsv);
        m_picker->m_colourPicker->updateDraggerWithHSV(m_picker->m_hsv);
    }
}

const ccColor3B ColorPicker::getColor() {
    return m_picker->m_rgb;
}

void ColorPicker::setEnabled(bool enabled) {
    if (m_picker->m_huePicker) {
        m_picker->m_huePicker->setEnabled(enabled);
    }
    if (m_picker->m_colourPicker) {
        m_picker->m_colourPicker->setEnabled(enabled);
    }

    GLubyte dim = enabled ? 255 : 100;
    if (auto* batch = m_picker->getChildByType<CCSpriteBatchNode>(0)) {
        for (auto* sprite : batch->getChildrenExt<CCSprite*>()) {
            sprite->setOpacity(dim);
        }
    }
}
