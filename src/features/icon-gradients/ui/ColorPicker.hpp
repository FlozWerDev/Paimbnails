#pragma once
#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCControlColourPicker.h>

namespace paimon::icon_gradients {

using namespace geode::prelude;

class ColorPicker : public CCNode {

private:

    CCControlColourPicker* m_picker = nullptr;

    bool init() override;

public:

    static ColorPicker* create();

    void setDelegate(ColorPickerDelegate*);
    void setColor(const ccColor3B&);
    void setEnabled(bool);

    const ccColor3B getColor();

};

} // namespace paimon::icon_gradients
