#pragma once
#include <Geode/Geode.hpp>

#include "../GradientTypes.hpp"
#include "ColorNode.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class PointsLayer : public CCLayer {

private:

    // preview icon and its drop shadow.
    SimplePlayer* m_icon = nullptr;
    CCSprite* m_shadow = nullptr;

    // owning editor.
    GradientLayer* m_layer = nullptr;

    IconType m_type = IconType::Cube;
    GradientConfig m_currentConfig;
    ColorType m_currentColor = ColorType::Main;

    // live points, fading ghosts, and interaction focus.
    std::vector<ColorNode*> m_points;
    std::vector<ColorNode*> m_removingPoints;
    // focused points.
    ColorNode* m_selectedPoint = nullptr;
    ColorNode* m_hoveredPoint = nullptr;

    CCPoint m_moveOffset = ccp(0, 0);
    CCPoint m_pointOffset = ccp(0, 0);

    bool m_isLinear = true;
    bool m_isMoving = false;
    bool m_isAnimating = false;
    // editor flags.
    bool m_ignoreColorChange = false;
    bool m_pointsHidden = false;

    bool init(CCSize, CCPoint);

    // touch handling.
    bool ccTouchBegan(CCTouch*, CCEvent*) override;
    void ccTouchMoved(CCTouch*, CCEvent*) override;
    void ccTouchEnded(CCTouch*, CCEvent*) override;

    // geometry helpers.
    CCPoint clampPos(CCPoint);
    CCPoint getRelativePos(ColorNode*);
    void updateCenter();

    // point management.
    void addRealPoints();
    void addPoint(const CCPoint&, bool = false);
    void selectPoint(ColorNode*);

    void onAnimationEnded();

public:

    static PointsLayer* create(const CCSize&, GradientLayer*, CCPoint);

    // lookup and icon access.
    ColorNode* getNodeForPos(CCPoint);
    ColorNode* getSelectedPoint();
    SimplePlayer* getIcon();

    // snapshot queries.
    std::vector<SimplePoint> getPoints();
    IconType getType();
    int getPointCount();

    // hover and point styling.
    void updateHover(const CCPoint&);
    void updatePointOpacity(int);
    void updatePointScale(float);

    // preview refresh.
    void updateGradient(GradientConfig, ColorType, bool = false);
    void setPlayerFrame(IconType);

    // visibility.
    void setPointsHidden(bool, float);

    // selection.
    void selectFirst();
    void selectLast();
    void removeSelected();
    // offset moves.
    void moveSelected(const CCPoint&);

    // point creation.
    void addPoint();
    void loadPoints(GradientConfig, bool = true);

};

} // namespace paimon::icon_gradients
