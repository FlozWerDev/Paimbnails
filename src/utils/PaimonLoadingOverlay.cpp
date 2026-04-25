#include "PaimonLoadingOverlay.hpp"
#include <random>

using namespace cocos2d;

namespace {
constexpr auto kSpinnerPulseKey = "flozwer.paimbnails/loading-spinner-pulse";
}

static std::string getRandomFunFact() {
    static const std::vector<std::string> facts = {
        "Paimbnails is Paimon Thumbnails!",
        "Made with love by Flozwer",
        "Did you know? You can rate thumbnails!",
        "Thumbnails make levels stand out",
        "Paimon approves this thumbnail",
        "Over thousands of thumbnails uploaded!",
        "You can capture your own thumbnails in-game",
        "Try zooming into the thumbnail preview!",
        "Paimbnails supports GIFs too!",
        "The community makes the best thumbnails",
        "Every level deserves a good thumbnail",
        "Tip: Use high quality settings for captures",
        "Paimon is always watching your thumbnails",
        "Fun fact: Thumbnails are cached locally",
        "You can suggest thumbnails for any level",
        "Moderators keep the quality high!",
        "Profile backgrounds are a thing too!",
        "Paimbnails - making GD prettier since 2024",
        "The leaderboard tracks top contributors",
        "Emergency food? No, emergency thumbnails!",
    };
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, facts.size() - 1);
    return facts[dist(rng)];
}

PaimonLoadingOverlay* PaimonLoadingOverlay::create(std::string const& statusText, float spinnerSize) {
    auto ret = new PaimonLoadingOverlay();
    if (ret && ret->init(statusText, spinnerSize)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonLoadingOverlay::init(std::string const& statusText, float spinnerSize) {
    if (!CCLayerColor::initWithColor({0, 0, 0, 0})) return false;

    this->setID("paimon-loading-overlay"_spr);

    // spinner centrado (posicion se ajusta en show())
    m_spinner = geode::LoadingSpinner::create(spinnerSize);
    m_spinner->setScale(0.f);
    m_spinner->setID("paimon-loading-spinner"_spr);
    this->addChild(m_spinner, 10);

    // pulso sutil en loop
    auto pulse = CCRepeatForever::create(
        CCSequence::create(
            CCEaseInOut::create(CCScaleTo::create(0.8f, 1.08f), 2.0f),
            CCEaseInOut::create(CCScaleTo::create(0.8f, 0.95f), 2.0f),
            nullptr
        )
    );
    pulse->setTag(99);

    // texto de estado debajo del spinner (dorado)
    m_statusLabel = CCLabelBMFont::create(statusText.c_str(), "goldFont.fnt");
    m_statusLabel->setScale(0.5f);
    m_statusLabel->setOpacity(0);
    m_statusLabel->setID("paimon-loading-text"_spr);
    this->addChild(m_statusLabel, 10);

    // fun fact random (blanco, semi-transparente)
    m_funFactLabel = CCLabelBMFont::create(getRandomFunFact().c_str(), "chatFont.fnt");
    m_funFactLabel->setScale(0.55f);
    m_funFactLabel->setColor({255, 255, 255});
    m_funFactLabel->setOpacity(0);
    m_funFactLabel->setID("paimon-loading-funfact"_spr);
    this->addChild(m_funFactLabel, 10);

    // guardar pulse para aplicar despues del bounce-in
    m_spinner->setUserObject(kSpinnerPulseKey, pulse);

    return true;
}

void PaimonLoadingOverlay::showAt(CCNode* parent, CCPoint const& position, CCSize const& size, int zOrder) {
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});
    this->setPosition(position);

    float cx = size.width / 2.f;
    float cy = size.height / 2.f;
    m_spinner->setPosition({cx, cy + 10.f});
    m_statusLabel->setPosition({cx, cy - 25.f});
    m_funFactLabel->setPosition({cx, cy - 45.f});

    parent->addChild(this, zOrder);

    this->runAction(CCFadeTo::create(0.25f, 100));

    m_spinner->runAction(
        CCSequence::create(
            CCEaseBackOut::create(CCScaleTo::create(0.3f, 1.0f)),
            CCCallFunc::create(this, callfunc_selector(PaimonLoadingOverlay::startPulse)),
            nullptr
        )
    );

    m_statusLabel->runAction(
        CCSequence::create(
            CCDelayTime::create(0.15f),
            CCFadeIn::create(0.2f),
            nullptr
        )
    );

    m_funFactLabel->runAction(
        CCSequence::create(
            CCDelayTime::create(0.35f),
            CCFadeTo::create(0.3f, 90),
            nullptr
        )
    );
}

void PaimonLoadingOverlay::show(CCNode* parent, int zOrder) {
    if (!parent) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    auto localOrigin = parent->convertToNodeSpace(CCPointZero);
    showAt(parent, localOrigin, winSize, zOrder);
}

void PaimonLoadingOverlay::showLocal(CCNode* parent, int zOrder) {
    if (!parent) return;

    auto size = parent->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) {
        size = CCDirector::sharedDirector()->getWinSize();
    }
    showAt(parent, CCPointZero, size, zOrder);
}

void PaimonLoadingOverlay::startPulse() {
    if (m_spinner) {
        auto pulse = static_cast<CCAction*>(m_spinner->getUserObject(kSpinnerPulseKey));
        if (pulse) {
            m_spinner->runAction(static_cast<CCAction*>(pulse->copy()->autorelease()));
        }
    }
}

void PaimonLoadingOverlay::dismiss() {
    if (m_dismissed) return;
    m_dismissed = true;

    // fade-out del fondo
    this->runAction(CCFadeTo::create(0.2f, 0));

    // shrink + fade del spinner
    if (m_spinner) {
        m_spinner->stopActionByTag(99);
        m_spinner->runAction(
            CCSpawn::create(
                CCEaseBackIn::create(CCScaleTo::create(0.15f, 0.f)),
                CCFadeOut::create(0.15f),
                nullptr
            )
        );
    }

    // fade-out del texto + cleanup
    if (m_statusLabel) {
        m_statusLabel->runAction(CCFadeOut::create(0.1f));
    }
    if (m_funFactLabel) {
        m_funFactLabel->runAction(CCFadeOut::create(0.1f));
    }

    // removeFromParent despues de que termine la animacion
    this->runAction(
        CCSequence::create(
            CCDelayTime::create(0.25f),
            CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        )
    );
}

void PaimonLoadingOverlay::updateText(std::string const& text) {
    if (m_statusLabel) {
        m_statusLabel->setString(text.c_str());
    }
}
