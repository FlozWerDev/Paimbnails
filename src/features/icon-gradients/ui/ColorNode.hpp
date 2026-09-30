#pragma once
#include <Geode/Geode.hpp>

namespace paimon::icon_gradients {

using namespace geode::prelude;

class ColorNode : public CCNode {

private:

    bool init(bool);

    // sprites: dot, flash ring, selection ring.
    CCSprite* m_dot = nullptr;
    CCSprite* m_circle = nullptr;
    CCSprite* m_select = nullptr;

    ccColor3B m_color = ccc3(255, 255, 255);
    std::string m_imagePath;
    CCLabelBMFont* m_imageLabel = nullptr;

    bool m_isHovered = false;
    bool m_isSelected = false;

    // visibility state.
    bool m_isHidden = false;
    bool m_isAnimating = false;

    int m_opacity = 255;

    // delayed reconcile shared by the fade paths.
    void settleAfter(float);

public:

    static ColorNode* create(bool, int = 255);

    // appearance.
    void setColor(const ccColor3B&, float = 0.f);
    void setOpacity(int);
    void setImagePath(std::string const&);
    std::string const& getImagePath() const { return m_imagePath; }

    // interaction state.
    void setSelected(bool);
    void setHovered(bool);
    void setHidden(bool, float, bool = false);

    // point queries.
    ccColor3B getColor();
    CCSprite* getSprite();

    // flag queries.
    bool isSelected();
    bool isHidden();
    bool isAnimating();

    // effects.
    void flash(float = 0.3f);

    void onAnimationEnded();

};

} // namespace paimon::icon_gradients
