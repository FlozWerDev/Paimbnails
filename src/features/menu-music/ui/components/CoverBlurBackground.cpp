#include "CoverBlurBackground.hpp"
#include "../../../../blur/BlurSystem.hpp"
#include "../../../../utils/SpriteHelper.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/utils/cocos.hpp>
#include <fmt/format.h>

using namespace geode::prelude;

namespace paimon::menumusic {

CoverBlurBackground* CoverBlurBackground::create(CCSize const& size) {
    auto ret = new CoverBlurBackground();
    if (ret && ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CoverBlurBackground::init(CCSize const& size) {
    if (!CCNode::init()) return false;
    m_size = size;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});
    return true;
}

void CoverBlurBackground::setCoverFromPath(const std::string& absolutePath) {
    // Invalida cualquier blur async aun en vuelo.
    m_generation++;
    auto gen = m_generation;
    m_lastPath = absolutePath;

    if (absolutePath.empty()) {
        if (m_currentBlur) {
            m_currentBlur->removeFromParent();
            m_currentBlur = nullptr;
        }
        return;
    }

    auto* source = CCTextureCache::sharedTextureCache()->addImage(absolutePath.c_str(), false);
    if (!source) return;

    std::string key = fmt::format("menumusic_cover::{}", absolutePath);

    float intensity = static_cast<float>(
        Mod::get()->getSavedValue<double>("menuMusicBlurIntensity", 5.0));
    if (intensity <= 0.f) intensity = 6.f;

    auto callback = [this, gen](CCSprite* blurred) {
        if (!blurred) return;
        // gen distinto => llego otro setCoverFromPath: descartar callback obsoleto.
        if (gen != m_generation) return;
        applyBlurFromTexture(blurred->getTexture(), gen);
    };

    BlurSystem::getInstance()->buildPaimonBlurPriority(
        source,
        m_size,
        intensity,
        std::move(key),
        callback
    );
}

void CoverBlurBackground::applyBlurFromTexture(CCTexture2D* tex, std::uint64_t generation) {
    if (generation != m_generation || !tex) return;

    auto newSprite = CCSprite::createWithTexture(tex);
    if (!newSprite) return;

    const CCSize ts = newSprite->getContentSize();
    float sx = m_size.width / ts.width;
    float sy = m_size.height / ts.height;
    float scale = std::max(sx, sy);
    newSprite->setScale(scale);
    newSprite->setAnchorPoint({0.5f, 0.5f});
    newSprite->setPosition(m_size / 2);
    newSprite->setOpacity(0);

    this->addChild(newSprite, 0);

    auto fadeIn = CCFadeTo::create(0.25f, 255);
    newSprite->runAction(fadeIn);

    if (m_currentBlur) {
        auto oldSprite = m_currentBlur;
        auto fadeOut = CCFadeTo::create(0.25f, 0);
        auto remove = CCCallFunc::create(oldSprite, callfunc_selector(CCNode::removeFromParent));
        oldSprite->runAction(CCSequence::create(fadeOut, remove, nullptr));
    }
    m_currentBlur = newSprite;
}

} // namespace paimon::menumusic
