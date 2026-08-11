#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace paimon::icon_gradients {

using namespace geode::prelude;

class GradientLayer;

class ColorSelectLayer : public Popup {

private:

    GradientLayer* m_layer = nullptr;

    bool init() override;

    void createButton(int, const CCPoint&);

    void onColor(CCObject*);

public:

    static ColorSelectLayer* create(GradientLayer*);

};

} // namespace paimon::icon_gradients
