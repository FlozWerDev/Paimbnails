#pragma once

// Live preview of a configured layer, including the same image/GIF/video/shader
// pipeline and a small vanilla UI mockup. Animated sources keep running.

#include <Geode/Geode.hpp>
#include <string>

#include "../services/LayerBackgroundManager.hpp"

namespace paimon::bgpreview {

// Build a vanilla UI mockup in winSize coordinates.
cocos2d::CCNode* createSceneMock(std::string const& layerKey);

// Localized display name, falling back to the layer key.
std::string displayNameForLayer(std::string const& layerKey);

std::string describeConfig(LayerBgConfig const& cfg);

// Status-dot color (gray means default).
cocos2d::ccColor3B accentForType(std::string const& type);

class LayerPreviewNode : public cocos2d::CCNode {
public:
    // Fit the real screen aspect ratio inside box.
    static LayerPreviewNode* create(cocos2d::CCSize box, std::string const& layerKey);

    void setLayerKey(std::string const& key);
    void setShowMock(bool show);
    bool showsMock() const { return m_showMock; }

    void refresh();

    cocos2d::CCSize frameSize() const { return m_frame; }

    // Config resolved through the "Same as..." chain.
    LayerBgConfig resolvedConfig() const;

protected:
    bool initWithBox(cocos2d::CCSize box, std::string const& layerKey);

    void rebuildBackground();
    void rebuildMock();

    // Scale a sprite to cover the frame and add it to the clipper.
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

    // Invalidates async thumbnail/GIF callbacks on refresh.
    int m_gen = 0;
};

}
