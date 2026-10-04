#include "ThumbViewerChrome.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::thumbviewer {

constexpr float kBarHeight = 30.f;

InfoBar* InfoBar::create(float width) {
    auto ret = new InfoBar();
    if (ret && ret->init(width)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool InfoBar::init(float width) {
    if (!CCNode::init()) return false;
    m_width = width;
    this->setContentSize({width, kBarHeight});
    this->setAnchorPoint({0.5f, 0.5f});

    auto bg = paimon::ui::makeInset({width, kBarHeight}, 150);
    bg->setPosition({0.f, 0.f});
    this->addChild(bg, -1);

    m_titleLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setScale(0.34f);
    m_titleLabel->setPosition({10.f, kBarHeight - 10.f});
    this->addChild(m_titleLabel);

    m_creatorLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_creatorLabel->setAnchorPoint({0.f, 0.5f});
    m_creatorLabel->setScale(0.44f);
    m_creatorLabel->setColor(paimon::ui::palette::muted);
    m_creatorLabel->setPosition({10.f, 9.f});
    this->addChild(m_creatorLabel);

    m_starSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (!m_starSprite) m_starSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
    if (m_starSprite) {
        m_starSprite->setScale(0.4f);
        m_starSprite->setPosition({width - 86.f, kBarHeight * 0.5f});
        this->addChild(m_starSprite);
    }

    m_ratingLabel = CCLabelBMFont::create("...", "goldFont.fnt");
    m_ratingLabel->setAnchorPoint({0.f, 0.5f});
    m_ratingLabel->setScale(0.36f);
    m_ratingLabel->setPosition({width - 74.f, kBarHeight * 0.5f});
    this->addChild(m_ratingLabel);

    m_counterLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_counterLabel->setAnchorPoint({1.f, 0.5f});
    m_counterLabel->setScale(0.34f);
    m_counterLabel->setPosition({width - 10.f, kBarHeight - 9.f});
    m_counterLabel->setVisible(false);
    this->addChild(m_counterLabel);

    m_dotsHolder = CCNode::create();
    m_dotsHolder->setPosition({width - 42.f, 8.f});
    this->addChild(m_dotsHolder);

    return true;
}

void InfoBar::setLevelId(int32_t levelID) {
    m_titleLabel->setString(fmt::format("ID {}", levelID).c_str());
}

void InfoBar::setCreator(std::string const& creator) {
    if (creator.empty() || creator == "Unknown") {
        m_creatorLabel->setString("");
        return;
    }
    m_creatorLabel->setString(fmt::format("por {}", creator).c_str());
    float maxW = m_width * 0.45f;
    if (m_creatorLabel->getScaledContentSize().width > maxW) {
        m_creatorLabel->setScale(0.44f * (maxW / m_creatorLabel->getScaledContentSize().width));
    }
}

void InfoBar::setRating(bool hasData, float average, int count) {
    if (!m_ratingLabel) return;
    if (!hasData) {
        m_ratingLabel->setString("...");
        m_ratingLabel->setColor({255, 255, 255});
        return;
    }
    m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
    m_ratingLabel->setColor(count == 0 ? ccColor3B{255, 100, 100} : ccColor3B{255, 255, 255});
}

void InfoBar::setCounter(int index, int total) {
    bool multi = total > 1;
    m_counterLabel->setVisible(multi);
    if (multi) m_counterLabel->setString(fmt::format("{} / {}", index + 1, total).c_str());
    rebuildDots(index, total);
}

void InfoBar::rebuildDots(int index, int total) {
    if (!m_dotsHolder) return;
    m_dotsHolder->removeAllChildren();
    // dots get noisy past this; the counter carries the rest.
    if (total <= 1 || total > 8) return;

    float gap = 9.f;
    float startX = -(static_cast<float>(total - 1) * gap) * 0.5f;
    for (int i = 0; i < total; ++i) {
        char const* frame = (i == index) ? "gj_navDotBtn_on_001.png" : "gj_navDotBtn_off_001.png";
        auto dot = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (!dot) dot = paimon::SpriteHelper::safeCreateWithFrameName("uiDot_001.png");
        if (!dot) break;
        dot->setScale(0.5f);
        dot->setPosition({startX + i * gap, 0.f});
        m_dotsHolder->addChild(dot);
    }
}

CCMenuItemSpriteExtra* makeToolButton(char const* frame, float iconScale, char const* caption,
    CCObject* target, SEL_MenuHandler cb, ccColor3B tint) {
    auto holder = CCNode::create();

    auto icon = paimon::SpriteHelper::safeCreateWithFrameName(frame);
    if (!icon) return nullptr;
    icon->setScale(iconScale);
    if (tint.r != 255 || tint.g != 255 || tint.b != 255) icon->setColor(tint);

    float capH = 0.f;
    CCLabelBMFont* cap = nullptr;
    if (caption && caption[0]) {
        cap = CCLabelBMFont::create(caption, "chatFont.fnt");
        cap->setScale(0.34f);
        cap->setColor(paimon::ui::palette::muted);
        capH = cap->getScaledContentSize().height + 2.f;
    }

    float iconH = icon->getScaledContentSize().height;
    float iconW = icon->getScaledContentSize().width;
    float totalH = iconH + capH;
    float totalW = std::max(iconW, cap ? cap->getScaledContentSize().width : 0.f);

    holder->setContentSize({totalW, totalH});
    icon->setPosition({totalW * 0.5f, totalH - iconH * 0.5f});
    holder->addChild(icon);
    if (cap) {
        cap->setPosition({totalW * 0.5f, capH * 0.5f});
        holder->addChild(cap);
    }

    return CCMenuItemSpriteExtra::create(holder, target, cb);
}

CCNode* makeToolDivider(float height) {
    auto node = CCNode::create();
    node->setContentSize({2.f, height});
    auto line = CCLayerColor::create({255, 210, 90, 70});
    line->setContentSize({2.f, height});
    line->ignoreAnchorPointForPosition(false);
    line->setAnchorPoint({0.5f, 0.5f});
    line->setPosition({1.f, height * 0.5f});
    node->addChild(line);
    return node;
}

}
