#include "ThumbViewerChrome.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::thumbviewer {

constexpr float kBarHeight = 26.f;

CCRect imageArea(CCSize const& popupSize) {
    return {10.f, 10.f, popupSize.width - 106.f, popupSize.height - 52.f};
}

CCRect actionArea(CCSize const& popupSize) {
    return {popupSize.width - 86.f, 10.f, 76.f, popupSize.height - 52.f};
}

CCRect focusedImageArea(CCSize const& popupSize) {
    return {10.f, 10.f, popupSize.width - 20.f, popupSize.height - 20.f};
}

void setChromeFade(CCNode* node, float opacity) {
    if (!node) return;
    opacity = std::clamp(opacity, 0.f, 1.f);
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        auto const key = "thumbnail-focus-opacity"_spr;
        auto* saved = typeinfo_cast<CCInteger*>(node->getUserObject(key));
        GLubyte const base = saved ? static_cast<GLubyte>(saved->getValue()) : rgba->getOpacity();
        if (!saved && opacity < 1.f) node->setUserObject(key, CCInteger::create(base));
        rgba->setOpacity(static_cast<GLubyte>(std::round(base * opacity)));
        if (opacity >= 1.f) node->setUserObject(key, nullptr);
        // these nodes forward opacity to their own glyphs/sprites already.
        if (rgba->isCascadeOpacityEnabled() || typeinfo_cast<CCLabelBMFont*>(node)
            || typeinfo_cast<CCMenuItemSprite*>(node)
            || typeinfo_cast<cocos2d::extension::CCScale9Sprite*>(node)) return;
    }
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) setChromeFade(child, opacity);
}

InfoBar* InfoBar::create(float width) {
    auto ret = new InfoBar();
    if (ret && ret->initWithWidth(width)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool InfoBar::initWithWidth(float width) {
    if (!CCNode::init()) return false;
    m_width = width;
    this->setContentSize({width, kBarHeight});
    this->setAnchorPoint({0.5f, 0.5f});

    m_titleLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_titleLabel->setAnchorPoint({0.f, 0.5f});
    m_titleLabel->setScale(0.36f);
    m_titleLabel->setPosition({0.f, kBarHeight - 9.f});
    this->addChild(m_titleLabel);

    m_creatorLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_creatorLabel->setAnchorPoint({0.f, 0.5f});
    m_creatorLabel->setScale(0.42f);
    m_creatorLabel->setColor(paimon::ui::palette::muted);
    m_creatorLabel->setPosition({0.f, 7.f});
    this->addChild(m_creatorLabel);

    m_starSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
    if (!m_starSprite) m_starSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
    if (m_starSprite) {
        m_starSprite->setScale(0.34f);
        m_starSprite->setPosition({width - 88.f, kBarHeight - 9.f});
        this->addChild(m_starSprite);
    }

    m_ratingLabel = CCLabelBMFont::create("...", "goldFont.fnt");
    m_ratingLabel->setAnchorPoint({0.f, 0.5f});
    m_ratingLabel->setScale(0.32f);
    m_ratingLabel->setPosition({width - 78.f, kBarHeight - 9.f});
    this->addChild(m_ratingLabel);

    m_counterLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_counterLabel->setAnchorPoint({1.f, 0.5f});
    m_counterLabel->setScale(0.28f);
    m_counterLabel->setPosition({width, 7.f});
    m_counterLabel->setVisible(false);
    this->addChild(m_counterLabel);

    m_dotsHolder = CCNode::create();
    m_dotsHolder->setPosition({width - 96.f, 7.f});
    this->addChild(m_dotsHolder);

    return true;
}

void InfoBar::setLevelId(int32_t levelID) {
    m_titleLabel->setString(fmt::format("ID {}", levelID).c_str());
    m_titleLabel->limitLabelWidth(m_width - 126.f, 0.36f, 0.24f);
}

void InfoBar::setCreator(std::string const& creator) {
    if (creator.empty() || creator == "Unknown") {
        m_creatorLabel->setString("");
        return;
    }
    m_creatorLabel->setString(fmt::format("por {}", creator).c_str());
    m_creatorLabel->limitLabelWidth(m_width - 140.f, 0.42f, 0.24f);
}

void InfoBar::setRating(bool hasData, float average, int count) {
    if (!m_ratingLabel) return;
    if (!hasData) {
        m_ratingLabel->setString("...");
        m_ratingLabel->setColor({255, 255, 255});
        return;
    }
    m_ratingLabel->setString(fmt::format("{:.1f} ({})", average, count).c_str());
    m_ratingLabel->limitLabelWidth(78.f, 0.32f, 0.18f);
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

    float gap = 7.f;
    float startX = -(static_cast<float>(total - 1) * gap) * 0.5f;
    for (int i = 0; i < total; ++i) {
        char const* frame = (i == index) ? "gj_navDotBtn_on_001.png" : "gj_navDotBtn_off_001.png";
        auto dot = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (!dot) dot = paimon::SpriteHelper::safeCreateWithFrameName("uiDot_001.png");
        if (!dot) break;
        dot->setScale(0.4f);
        dot->setPosition({startX + i * gap, 0.f});
        m_dotsHolder->addChild(dot);
    }
}

CCMenuItemSpriteExtra* makeToolButton(char const* frame, float iconScale, char const* caption,
    CCObject* target, SEL_MenuHandler cb, ccColor3B tint, char const* background) {
    return makeToolButton(paimon::SpriteHelper::safeCreateWithFrameName(frame),
        iconScale, caption, target, cb, tint, background);
}

CCMenuItemSpriteExtra* makeToolButton(CCSprite* icon, float iconScale, char const* caption,
    CCObject* target, SEL_MenuHandler cb, ccColor3B tint, char const* background) {
    if (!icon) return nullptr;
    auto holder = CCNodeRGBA::create();
    holder->setCascadeOpacityEnabled(true);
    bool const hasCaption = caption && caption[0];
    float const width = hasCaption ? kToolWidth : 20.f;
    float const height = hasCaption ? 24.f : 20.f;
    holder->setContentSize({width, height});

    if (hasCaption) {
        auto* bg = paimon::ui::makeButtonSprite("", background,
            width, 0.65f, "goldFont.fnt");
        if (bg) {
            bg->setCascadeOpacityEnabled(true);
            float const backgroundHeight = bg->getScaledContentSize().height;
            if (backgroundHeight > 0.f) {
                bg->setScaleY(bg->getScaleY() * height / backgroundHeight);
            }
            bg->setPosition({width / 2.f, height / 2.f});
            holder->addChild(bg, -1);
        }
    }

    auto const size = icon->getContentSize();
    float const iconSize = hasCaption ? 16.f : 18.f;
    icon->setScale(std::min({iconScale, iconSize / std::max(size.width, 1.f),
        iconSize / std::max(size.height, 1.f)}));
    if (tint.r != 255 || tint.g != 255 || tint.b != 255) icon->setColor(tint);
    icon->setPosition({hasCaption ? 12.f : width / 2.f, height / 2.f});
    holder->addChild(icon);

    if (hasCaption) {
        auto* cap = CCLabelBMFont::create(caption, "goldFont.fnt");
        cap->setAnchorPoint({0.f, 0.5f});
        cap->limitLabelWidth(width - 27.f, 0.36f, 0.22f);
        cap->setColor(paimon::ui::palette::text);
        cap->setPosition({24.f, height / 2.f});
        holder->addChild(cap);
    }

    return CCMenuItemSpriteExtra::create(holder, target, cb);
}

CCNode* makeToolDivider(float width) {
    auto node = CCNode::create();
    node->setContentSize({width, 4.f});
    auto line = CCLayerColor::create({255, 210, 90, 65}, width, 1.f);
    line->setPosition({0.f, 2.f});
    node->addChild(line);
    return node;
}

}
