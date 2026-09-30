#pragma once

// live preview running the real image/gif/video/shader pipeline plus a vanilla ui mockup.

#include <Geode/Geode.hpp>
#include <string>

#include "../services/LayerBackgroundManager.hpp"

namespace paimon::bgpreview {

// build a vanilla ui mockup in winsize coordinates.
cocos2d::CCNode* createSceneMock(std::string const& layerKey);

// localized display name, falling back to the layer key.
std::string displayNameForLayer(std::string const& layerKey);

std::string describeConfig(LayerBgConfig const& cfg);

class LayerPreviewNode : public cocos2d::CCNode {
public:
    // fit the real screen aspect ratio inside box.
    static LayerPreviewNode* create(cocos2d::CCSize box, std::string const& layerKey);

    void setLayerKey(std::string const& key);
    void setShowMock(bool show);
    bool showsMock() const { return m_showMock; }

    void refresh();

    cocos2d::CCSize frameSize() const { return m_frame; }

    // config resolved through the "same as..." chain.
    LayerBgConfig resolvedConfig() const;

protected:
    bool initWithBox(cocos2d::CCSize box, std::string const& layerKey);

    void rebuildBackground();
    void rebuildMock();

    // scale a sprite to cover the frame and add it to the clipper.
    void addCover(cocos2d::CCSprite* sprite);
    void addFlatFill(cocos2d::ccColor4B color);
    void addPlaceholder(std::string const& text, cocos2d::ccColor3B color);
    void addDarkOverlay(LayerBgConfig const& cfg);
    void applyPostShader(cocos2d::CCSprite* sprite, std::string const& shader);

    std::string m_key = "menu";
    cocos2d::CCSize m_frame{0.f, 0.f};
    cocos2d::CCNode* m_bgHolder = nullptr;
    cocos2d::CCNode* m_mockHolder = nullptr;
    bool m_showMock = true;

    // invalidates async thumbnail/gif callbacks on refresh.
    int m_gen = 0;
};

}
