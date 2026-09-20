#include "../services/LiveSlotRuntime.hpp"

#include <Geode/modify/CCSprite.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCTextureCache.hpp>
#include <Geode/modify/CommentCell.hpp>
#include <Geode/modify/CustomSFXCell.hpp>
#include <Geode/modify/LevelCell.hpp>
#include <Geode/modify/SmartTemplateCell.hpp>

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
        LiveSlotRuntime::get().refreshTextures();
        return texture;
    }
};

class $modify(PaimonLiveLevelCell, LevelCell) {
    void draw() {
        LevelCell::draw();
        // Runs after the original so it wins over updateBGColor every frame.
        if (!m_backgroundLayer) return;
        ccColor3B color;
        if (LiveSlotRuntime::get().cellColor(m_indexPath.m_row, &color)) {
            m_backgroundLayer->setColor(color);
        }
    }
};

class $modify(PaimonLiveCommentCell, CommentCell) {
    void draw() {
        CommentCell::draw();
        // Runs after the original so it wins over updateBGColor every frame.
        if (!m_backgroundLayer) return;
        ccColor3B color;
        if (LiveSlotRuntime::get().cellColor(m_indexPath.m_row, &color)) {
            m_backgroundLayer->setColor(color);
        }
    }
};

class $modify(PaimonLiveSFXCell, CustomSFXCell) {
    void updateBGColor(int index) {
        CustomSFXCell::updateBGColor(index);
        if (!m_backgroundLayer) return;
        ccColor3B color;
        if (LiveSlotRuntime::get().cellColor(index, &color)) {
            m_backgroundLayer->setColor(color);
        }
    }
};

class $modify(PaimonLiveTemplateCell, SmartTemplateCell) {
    void updateBGColor(int index) {
        SmartTemplateCell::updateBGColor(index);
        if (!m_backgroundLayer) return;
        ccColor3B color;
        if (LiveSlotRuntime::get().cellColor(index, &color)) {
            m_backgroundLayer->setColor(color);
        }
    }
};

$execute {
    listenForSettingChanges<bool>("texture-studio-enabled", +[](bool) {
        LiveSlotRuntime::get().restoreSaved();
    });
}
