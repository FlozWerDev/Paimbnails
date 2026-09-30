#include "PaimonLoadingOverlay.hpp"
#include "SheetAnimSprite.hpp"
#include "SpriteHelper.hpp"
#include <algorithm>
#include <cmath>
#include <random>

using namespace cocos2d;

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

// dots animate in-overlay; strip trailing ones from the base string.
static std::string stripTrailingDots(std::string s) {
    while (!s.empty() && s.back() == '.') s.pop_back();
    return s;
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
    m_baseText = stripTrailingDots(statusText);
    m_spinnerSize = spinnerSize;

    m_badge = CCNode::create();
    m_badge->setID("paimon-loading-badge"_spr);
    m_badge->setScale(0.f);
    this->addChild(m_badge, 10);

    if (auto ring = paimon::SpriteHelper::safeCreate("loadingCircle.png")) {
        ring->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        ring->setOpacity(220);
        float texW = ring->getContentSize().width;
        if (texW > 1.f) ring->setScale(spinnerSize * 2.2f / texW);
        ring->runAction(CCRepeatForever::create(CCRotateBy::create(1.1f, 360.f)));
        m_ring = ring;
        m_badge->addChild(ring, 0);
    }

    bool hasMascot = false;
    static thread_local std::mt19937 mascotRng(std::random_device{}());
    bool preferSheet = std::uniform_int_distribution<int>(0, 1)(mascotRng) == 0;

    auto addStaticMascot = [&]() -> bool {
        auto emote = paimon::SpriteHelper::safeCreate("paim_Paimon.png"_spr);
        if (!emote) return false;
        float h = emote->getContentSize().height;
        if (h > 1.f) emote->setScale(spinnerSize * 1.1f / h);
        emote->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineInOut::create(CCMoveBy::create(0.8f, {0.f, 5.f})),
            CCEaseSineInOut::create(CCMoveBy::create(0.8f, {0.f, -5.f})),
            nullptr
        )));
        emote->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineInOut::create(CCRotateTo::create(1.3f, 5.f)),
            CCEaseSineInOut::create(CCRotateTo::create(1.3f, -5.f)),
            nullptr
        )));
        m_badge->addChild(emote, 1);
        return true;
    };

    auto addSheetMascot = [&]() -> bool {
        auto* anim = SheetAnimSprite::createPaimonMascot();
        if (!anim) return false;
        float h = anim->getContentSize().height;
        if (h > 1.f) anim->setScale(spinnerSize * 1.1f / h);
        anim->setID("paimon-loading-mascot-sheet"_spr);
        m_badge->addChild(anim, 1);
        return true;
    };

    if (preferSheet) {
        hasMascot = addSheetMascot() || addStaticMascot();
    } else {
        hasMascot = addStaticMascot() || addSheetMascot();
    }

    // both textures missing (aggressive packs).
    if (!m_ring && !hasMascot) {
        m_spinner = geode::LoadingSpinner::create(spinnerSize);
        m_spinner->setID("paimon-loading-spinner"_spr);
        m_badge->addChild(m_spinner, 1);
    }

    m_statusLabel = CCLabelBMFont::create(m_baseText.c_str(), "goldFont.fnt");
    m_statusLabel->setScale(0.5f);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setOpacity(0);
    m_statusLabel->setID("paimon-loading-text"_spr);
    this->addChild(m_statusLabel, 10);

    m_funFactLabel = CCLabelBMFont::create(getRandomFunFact().c_str(), "chatFont.fnt");
    m_funFactLabel->setScale(0.55f);
    m_funFactLabel->setColor({255, 255, 255});
    m_funFactLabel->setOpacity(0);
    m_funFactLabel->setID("paimon-loading-funfact"_spr);
    this->addChild(m_funFactLabel, 10);

    this->schedule(schedule_selector(PaimonLoadingOverlay::updateDots), 0.4f);
    this->schedule(schedule_selector(PaimonLoadingOverlay::swapFunFact), 5.f);

    return true;
}

void PaimonLoadingOverlay::showAt(CCNode* parent, CCPoint const& position, CCSize const& size, int zOrder) {
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});
    this->setPosition(position);

    float cx = size.width / 2.f;
    float cy = size.height / 2.f;
    m_centerX = cx;
    m_statusY = cy + 14.f - m_spinnerSize * 1.35f - 12.f;

    m_badge->setPosition({cx, cy + 14.f});
    positionStatusLabel();
    m_funFactLabel->setPosition({cx, m_statusY - 18.f});

    parent->addChild(this, zOrder);

    this->setTouchEnabled(true);

    this->runAction(CCFadeTo::create(0.25f, 140));

    m_badge->runAction(
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

    // parent-space corners; scaled/offset parents leave no gaps.
    auto winSize = CCDirector::get()->getWinSize();
    auto bl = parent->convertToNodeSpace({0.f, 0.f});
    auto tr = parent->convertToNodeSpace({winSize.width, winSize.height});

    CCPoint origin{std::min(bl.x, tr.x), std::min(bl.y, tr.y)};
    CCSize size{std::abs(tr.x - bl.x), std::abs(tr.y - bl.y)};
    if (size.width < 1.f || size.height < 1.f) {
        origin = CCPointZero;
        size = winSize;
    }
    showAt(parent, origin, size, zOrder);
}

void PaimonLoadingOverlay::showLocal(CCNode* parent, int zOrder) {
    if (!parent) return;

    auto size = parent->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) {
        size = CCDirector::get()->getWinSize();
    }
    showAt(parent, CCPointZero, size, zOrder);
}

void PaimonLoadingOverlay::positionStatusLabel() {
    if (!m_statusLabel) return;
    // base text stays centered as dots grow; no wiggle.
    m_statusLabel->setString(m_baseText.c_str());
    float baseW = m_statusLabel->getScaledContentSize().width;
    m_statusLabel->setPosition({m_centerX - baseW / 2.f, m_statusY});
    m_statusLabel->setString((m_baseText + std::string(m_dotCount, '.')).c_str());
}

void PaimonLoadingOverlay::updateDots(float) {
    if (m_dismissed || !m_statusLabel) return;
    m_dotCount = (m_dotCount + 1) % 4;
    m_statusLabel->setString((m_baseText + std::string(m_dotCount, '.')).c_str());
}

void PaimonLoadingOverlay::pickNewFact() {
    if (m_funFactLabel) {
        m_funFactLabel->setString(getRandomFunFact().c_str());
    }
}

void PaimonLoadingOverlay::swapFunFact(float) {
    if (m_dismissed || !m_funFactLabel) return;
    m_funFactLabel->runAction(CCSequence::create(
        CCFadeTo::create(0.25f, 0),
        CCCallFunc::create(this, callfunc_selector(PaimonLoadingOverlay::pickNewFact)),
        CCFadeTo::create(0.3f, 90),
        nullptr
    ));
}

void PaimonLoadingOverlay::startPulse() {
    if (m_dismissed || !m_badge) return;
    m_badge->runAction(CCRepeatForever::create(
        CCSequence::create(
            CCEaseInOut::create(CCScaleTo::create(0.8f, 1.06f), 2.0f),
            CCEaseInOut::create(CCScaleTo::create(0.8f, 0.96f), 2.0f),
            nullptr
        )
    ));
}

void PaimonLoadingOverlay::dismiss() {
    if (m_dismissed) return;
    m_dismissed = true;

    this->unschedule(schedule_selector(PaimonLoadingOverlay::updateDots));
    this->unschedule(schedule_selector(PaimonLoadingOverlay::swapFunFact));

    this->setTouchEnabled(false);

    this->runAction(CCFadeTo::create(0.2f, 0));

    if (m_badge) {
        m_badge->stopAllActions();
        m_badge->runAction(CCEaseBackIn::create(CCScaleTo::create(0.18f, 0.f)));
    }
    if (m_statusLabel) {
        m_statusLabel->runAction(CCFadeOut::create(0.1f));
    }
    if (m_funFactLabel) {
        m_funFactLabel->stopAllActions();
        m_funFactLabel->runAction(CCFadeOut::create(0.1f));
    }

    this->runAction(
        CCSequence::create(
            CCDelayTime::create(0.25f),
            CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        )
    );
}

void PaimonLoadingOverlay::updateText(std::string const& text) {
    m_baseText = stripTrailingDots(text);
    positionStatusLabel();
}

void PaimonLoadingOverlay::registerWithTouchDispatcher() {
    // high priority + swallow; buttons below stay unpressable mid-load.
    CCTouchDispatcher::get()->addTargetedDelegate(this, -512, true);
}

bool PaimonLoadingOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_dismissed) return false;
    // only inside the covered area (showlocal may cover just a popup).
    auto p = this->convertToNodeSpace(touch->getLocation());
    auto s = this->getContentSize();
    return p.x >= 0.f && p.y >= 0.f && p.x <= s.width && p.y <= s.height;
}
