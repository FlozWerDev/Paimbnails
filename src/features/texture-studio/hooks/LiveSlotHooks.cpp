#include "../services/LiveSlotRuntime.hpp"

#include <Geode/modify/CCLayerColor.hpp>
#include <Geode/modify/CCSprite.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCTextureCache.hpp>

#include <algorithm>

using namespace geode::prelude;
using paimon::texture_studio::LiveSlotRuntime;

class $modify(PaimonLiveSlotSprite, CCSprite) {
    void draw() {
        auto* shader = LiveSlotRuntime::get().prepareDraw(getTexture(), getShaderProgram());
        if (!shader) return CCSprite::draw();
        Ref<CCGLProgram> previous = getShaderProgram();
        setShaderProgram(shader);
        CCSprite::draw();
        setShaderProgram(previous);
    }
};

class $modify(PaimonLiveSlotBatch, CCSpriteBatchNode) {
    void draw() {
        auto* shader = LiveSlotRuntime::get().prepareDraw(getTexture(), getShaderProgram());
        if (!shader) return CCSpriteBatchNode::draw();
        Ref<CCGLProgram> previous = getShaderProgram();
        setShaderProgram(shader);
        CCSpriteBatchNode::draw();
        setShaderProgram(previous);
    }
};

class $modify(PaimonLiveSlotTextures, CCTextureCache) {
    CCTexture2D* addImage(char const* path, bool skipSuffix) {
        auto* texture = CCTextureCache::addImage(path, skipSuffix);
        LiveSlotRuntime::get().onTextureLoaded(path, texture, skipSuffix);
        return texture;
    }

    CCTexture2D* addUIImage(CCImage* image, char const* key) {
        auto* texture = CCTextureCache::addUIImage(image, key);
        LiveSlotRuntime::get().onTextureLoaded(key, texture, true);
        return texture;
    }
};

// list backgrounds and every TableViewCell fill are CCLayerColors in GD's
// brown family. Repainting the vertex colors for this draw covers all cell
// types (levels, comments, scores, songs...) and wins over updateBGColor.
class $modify(PaimonLiveLayerColor, CCLayerColor) {
    void draw() {
        auto& runtime = LiveSlotRuntime::get();
        if (!runtime.enabled()) return CCLayerColor::draw();
        ccColor4F saved[4];
        std::copy(std::begin(m_pSquareColors), std::end(m_pSquareColors), saved);
        if (!runtime.remapLayerColors(m_pSquareColors, 4)) return CCLayerColor::draw();
        CCLayerColor::draw();
        std::copy(std::begin(saved), std::end(saved), std::begin(m_pSquareColors));
    }
};

$execute {
    listenForSettingChanges<bool>("texture-studio-enabled", +[](bool) {
        LiveSlotRuntime::get().restoreSaved();
    });
}
