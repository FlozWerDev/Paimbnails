#pragma once

#include <Geode/Geode.hpp>
#include "Constants.hpp"

class UIBorderHelper {
public:
    static void createBorder(
        float centerX, 
        float centerY, 
        float width, 
        float height,
        cocos2d::CCNode* parent,
        float thickness = PaimonConstants::BORDER_THICKNESS,
        cocos2d::ccColor4B color = {0, 0, 0, PaimonConstants::DARK_OVERLAY_ALPHA},
        int zOrder = 1
    );
    
    static cocos2d::CCLayerColor* createTopBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCLayerColor* createBottomBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCLayerColor* createLeftBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCLayerColor* createRightBorder(float centerX, float centerY, float width, float height, float thickness, cocos2d::ccColor4B color);
};
