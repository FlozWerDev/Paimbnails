#include "../services/LiveSlotRuntime.hpp"

#include <Geode/modify/CCSprite.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCTextureCache.hpp>

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

    void addImageAsyncCallBack(float dt) {
        CCTextureCache::addImageAsyncCallBack(dt);
        LiveSlotRuntime::get().refreshTextures();
    }
};

$execute {
    listenForSettingChanges<bool>("texture-studio-enabled", +[](bool) {
        LiveSlotRuntime::get().restoreSaved();
    });
}
