#pragma once
#include <Geode/Geode.hpp>

#include "../GradientTypes.hpp"
#include "ColorNode.hpp"

namespace paimon::icon_gradients {

class GradientLayer;

class PointsLayer : public CCLayer {

private:

    // Preview icon and its drop shadow.
    SimplePlayer* m_icon = nullptr;
    CCSprite* m_shadow = nullptr;

    // Owning editor.
    GradientLayer* m_layer = nullptr;

    IconType m_type = IconType::Cube;
    GradientConfig m_currentConfig;
    ColorType m_currentColor = ColorType::Main;

    // Live points, fading ghosts, and interaction focus.
    std::vector<ColorNode*> m_points;
    std::vector<ColorNode*> m_removingPoints;
    // Focused points.
    ColorNode* m_selectedPoint = nullptr;
    ColorNode* m_hoveredPoint = nullptr;

    CCPoint m_moveOffset = ccp(0, 0);
    CCPoint m_pointOffset = ccp(0, 0);

    bool m_isLinear = true;
    bool m_isMoving = false;
    bool m_isAnimating = false;
    // Editor flags.
    bool m_ignoreColorChange = false;
    bool m_pointsHidden = false;

    bool init(CCSize, CCPoint);

    // Touch handling.
    bool ccTouchBegan(CCTouch*, CCEvent*) override;
    void ccTouchMoved(CCTouch*, CCEvent*) override;
    void ccTouchEnded(CCTouch*, CCEvent*) override;

    // Geometry helpers.
    CCPoint clampPos(CCPoint);
    CCPoint getRelativePos(ColorNode*);
    void updateCenter();

    // Point management.
    void addRealPoints();
    void addPoint(const CCPoint&, bool = false);
    void selectPoint(ColorNode*);

    void onAnimationEnded();

public:

    static PointsLayer* create(const CCSize&, GradientLayer*, CCPoint);

    // Lookup and icon access.
    ColorNode* getNodeForPos(CCPoint);
    ColorNode* getSelectedPoint();
    SimplePlayer* getIcon();

    // Snapshot queries.
    std::vector<SimplePoint> getPoints();
    IconType getType();
    int getPointCount();

    // Hover and point styling.
    void updateHover(const CCPoint&);
    void updatePointOpacity(int);
    void updatePointScale(float);

    // Preview refresh.
    void updateGradient(GradientConfig, ColorType, bool = false);
    void setPlayerFrame(IconType);

    // Visibility.
    void setPointsHidden(bool, float);

    // Selection.
    void selectFirst();
    void selectLast();
    void removeSelected();
    // Offset moves.
    void moveSelected(const CCPoint&);

    // Point creation.
    void addPoint();
    void loadPoints(GradientConfig, bool = true);

};

} // namespace paimon::icon_gradients
