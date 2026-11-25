#include "CoverBlurBackground.hpp"
#include "../../../../blur/BlurSystem.hpp"
#include "../../../../utils/SpriteHelper.hpp"
#include "../../../../utils/TextureBudget.hpp"

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
    this->setID("paimon-bg-cover-blur"_spr);
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});
    return true;
}

void CoverBlurBackground::setCoverFromPath(const std::string& absolutePath) {
    float intensity = static_cast<float>(Mod::get()->getSavedValue<double>("menuMusicBlurIntensity", 5.0));
    if (intensity <= 0.f) intensity = 6.f;
    if (absolutePath == m_lastPath && intensity == m_lastIntensity && (m_currentBlur || m_blurPending)) return;
    auto const generation = ++m_generation;
    m_lastPath = absolutePath;
    m_lastIntensity = intensity;
    m_blurPending = false;

    if (absolutePath.empty()) {
        if (m_currentBlur) {
            m_currentBlur->removeFromParent();
            m_currentBlur = nullptr;
        }
        return;
    }

    m_blurPending = true;
    WeakRef<CoverBlurBackground> weak = this;
    paimon::image::loadBudgetedAsync(absolutePath, [weak, generation, intensity](CCTexture2D* texture) {
        auto self = weak.lock();
        if (!self || generation != self->m_generation) return;
        if (!texture) {
            self->m_blurPending = false;
            if (self->m_currentBlur) { self->m_currentBlur->removeFromParent(); self->m_currentBlur = nullptr; }
            return;
        }
        self->buildBlur(texture, generation, intensity);
    });
}

void CoverBlurBackground::buildBlur(CCTexture2D* texture, std::uint64_t generation, float intensity) {
    WeakRef<CoverBlurBackground> weak = this;
    BlurSystem::getInstance()->buildPaimonBlurAsync(texture, m_size, intensity,
        fmt::format("menumusic_cover::{}", m_lastPath), [weak, generation](CCSprite* blurred) {
            auto self = weak.lock();
            if (!self || generation != self->m_generation) return;
            self->m_blurPending = false;
            if (blurred) self->applyBlurFromTexture(blurred->getTexture(), generation);
        });
}

void CoverBlurBackground::onExit() {
    ++m_generation;
    m_blurPending = false;
    CCNode::onExit();
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
