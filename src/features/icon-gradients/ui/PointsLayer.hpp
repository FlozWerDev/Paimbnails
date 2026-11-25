#pragma once
#include <Geode/Geode.hpp>

#include "../GradientTypes.hpp"
#include "ColorNode.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class PointsLayer : public CCLayer {

private:
    using CCLayer::init;

    SimplePlayer* m_icon = nullptr;
    CCSprite* m_shadow = nullptr;

    GradientLayer* m_layer = nullptr;

    IconType m_type = IconType::Cube;
    GradientConfig m_currentConfig;
    ColorType m_currentColor = ColorType::Main;

    std::vector<ColorNode*> m_points;
    std::vector<ColorNode*> m_removingPoints;
    ColorNode* m_selectedPoint = nullptr;
    ColorNode* m_hoveredPoint = nullptr;

    CCPoint m_moveOffset = ccp(0, 0);
    CCPoint m_pointOffset = ccp(0, 0);

    bool m_isLinear = true;
    bool m_isMoving = false;
    bool m_isAnimating = false;
    bool m_ignoreColorChange = false;
    bool m_pointsHidden = false;

    bool init(CCSize, CCPoint);

    bool ccTouchBegan(CCTouch*, CCEvent*) override;
    void ccTouchMoved(CCTouch*, CCEvent*) override;
    void ccTouchEnded(CCTouch*, CCEvent*) override;

    CCPoint clampPos(CCPoint);
    CCPoint getRelativePos(ColorNode*);
    void updateCenter();

    void addRealPoints();
    void addPoint(const CCPoint&, bool = false);
    void selectPoint(ColorNode*);

    void onAnimationEnded();

public:

    static PointsLayer* create(const CCSize&, GradientLayer*, CCPoint);

    ColorNode* getNodeForPos(CCPoint);
    ColorNode* getSelectedPoint();
    SimplePlayer* getIcon();

    std::vector<SimplePoint> getPoints();
    IconType getType();
    int getPointCount();

    void updateHover(const CCPoint&);
    void updatePointOpacity(int);
    void updatePointScale(float);

    void updateGradient(GradientConfig, ColorType, bool = false);
    void setPlayerFrame(IconType);

    void setPointsHidden(bool, float);

    void selectFirst();
    void selectLast();
    void removeSelected();
    void moveSelected(const CCPoint&);

    void addPoint();
    void loadPoints(GradientConfig, bool = true);

};

} // namespace paimon::icon_gradients
