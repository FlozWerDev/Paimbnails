#include "RequestWidgets.hpp"

#include "../TwitchRequestNotify.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace paimon::twitch::widgets {

namespace {

constexpr int kBounceTag = 0x7b3;
constexpr float kPad = 7.f;
constexpr float kDotRoom = 10.f;

ButtonFace* faceOf(CCMenuItemSpriteExtra* button) {
    return button ? typeinfo_cast<ButtonFace*>(button->getNormalImage()) : nullptr;
}

}

ButtonFace* ButtonFace::create(ButtonSpec const& spec) {
    auto* ret = new ButtonFace();
    if (ret->init(spec)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ButtonFace::init(ButtonSpec const& spec) {
    if (!CCNodeRGBA::init()) return false;
    m_tone = spec.tone;

    float const scale = std::max(spec.height, 14.f) / 30.f;
    m_bg = paimon::ui::makeButtonSprite("", spec.tone.texture, std::max(spec.width, 18.f), scale);
    if (!m_bg) return false;

    auto const size = m_bg->getScaledContentSize();
    m_width = size.width;
    m_height = size.height;
    // narrow tiles need most of their width for the glyph.
    m_pad = std::min(kPad, m_width * 0.16f);
    m_labelScale = m_height * (0.8f / 30.f) * (spec.fontScale / 0.4f);

    setContentSize(size);
    setAnchorPoint({0.5f, 0.5f});
    ignoreAnchorPointForPosition(false);
    m_bg->setPosition(size / 2.f);
    addChild(m_bg, 0);

    m_label = CCLabelBMFont::create(spec.label.c_str(), "goldFont.fnt");
    m_label->setAnchorPoint({0.f, 0.5f});
    addChild(m_label, 5);

    if (!spec.badge.empty()) {
        auto* text = CCLabelBMFont::create(spec.badge.c_str(), "goldFont.fnt");
        text->setScale(m_height * 0.012f);
        m_chipWidth = text->getScaledContentSize().width + 9.f;
        float const chipHeight = m_height - 10.f;
        m_chip = CCNode::create();
        m_chip->setContentSize({m_chipWidth, chipHeight});
        m_chip->setPosition({m_width - 6.f - m_chipWidth, 5.f});
        if (auto* plate = paimon::ui::makeInset({m_chipWidth, chipHeight}, 120)) {
            m_chip->addChild(plate, 0);
        }
        text->setPosition({m_chipWidth / 2.f, chipHeight / 2.f + 0.5f});
        m_chip->addChild(text, 1);
        addChild(m_chip, 5);
    }

    if (spec.icon) setIcon(spec.icon, spec.iconRotation);
    else relayout();
    return true;
}

void ButtonFace::relayout() {
    float const chipRoom = m_chip ? m_chipWidth + 6.f : 0.f;
    float const room = m_width - 2.f * m_pad - chipRoom;
    float const dotRoom = m_dot ? kDotRoom : 0.f;
    float iconBox = 0.f;
    if (m_icon) {
        auto const size = m_icon->getContentSize();
        float const target = m_height * 0.62f;
        m_icon->setScale(target / std::max({size.width, size.height, 1.f}));
        iconBox = target;
    }
    bool const hasText = m_label->getString() && m_label->getString()[0] != '\0';
    float const gap = (m_icon && hasText) ? 4.f : 0.f;

    m_label->setScale(m_labelScale);
    if (hasText) {
        m_label->limitLabelWidth(
            std::max(10.f, room - dotRoom - iconBox - gap), m_labelScale, 0.1f);
    }
    float const labelWidth = hasText ? m_label->getScaledContentSize().width : 0.f;
    float const group = dotRoom + iconBox + gap + labelWidth;
    float const start = m_pad + (room - group) / 2.f;

    if (m_dot) m_dot->setPosition({start + 3.f, m_height / 2.f});
    if (m_icon) m_icon->setPosition({start + dotRoom + iconBox / 2.f, m_height / 2.f});
    m_label->setPosition({start + dotRoom + iconBox + gap, m_height / 2.f + m_height * 0.05f});
}

void ButtonFace::setLabel(std::string const& text) {
    if (!m_label || text == m_label->getString()) return;
    m_label->setString(text.c_str());
    relayout();
}

void ButtonFace::setIcon(char const* frame, float rotation) {
    if (m_icon) {
        m_icon->removeFromParent();
        m_icon = nullptr;
    }
    if (frame) {
        m_icon = paimon::SpriteHelper::safeCreateWithFrameName(frame);
        if (m_icon) {
            m_icon->setRotation(rotation);
            addChild(m_icon, 5);
        }
    }
    relayout();
}

void ButtonFace::setTone(Tone tone) {
    if (std::string_view(m_tone.texture) == tone.texture) return;
    m_tone = tone;
    m_bg->updateBGImage(tone.texture);
    m_bg->setPosition({m_width / 2.f, m_height / 2.f});
}

void ButtonFace::setMuted(bool muted) {
    m_bg->setColor(muted ? ccColor3B{140, 140, 140} : ccColor3B{255, 255, 255});
    m_label->setOpacity(muted ? 150 : 255);
    if (m_icon) m_icon->setOpacity(muted ? 150 : 255);
}

void ButtonFace::setDot(ccColor3B color, bool lit) {
    ccColor4B const paint{color.r, color.g, color.b, static_cast<GLubyte>(lit ? 255 : 150)};
    if (m_dot && m_dotPaint.r == paint.r && m_dotPaint.g == paint.g
        && m_dotPaint.b == paint.b && m_dotPaint.a == paint.a) return;
    m_dotPaint = paint;

    bool const created = !m_dot;
    if (created) {
        m_dot = CCNode::create();
        m_dot->setContentSize({6.f, 6.f});
        addChild(m_dot, 5);
    }
    m_dot->removeAllChildren();
    if (auto* dot = paimon::SpriteHelper::createRoundedRect(
            6.f, 6.f, 3.f, ccc4FFromccc3B(color))) {
        dot->setPosition({-3.f, -3.f});
        dot->setOpacity(paint.a);
        m_dot->addChild(dot);
    }
    if (created) relayout();
    else if (lit) bounce(m_dot);
}

CCMenuItemSpriteExtra* makeButton(ButtonSpec const& spec, std::function<void()> action) {
    auto* face = ButtonFace::create(spec);
    if (!face) return nullptr;
    return CCMenuItemExt::createSpriteExtra(face,
        [action = std::move(action)](CCMenuItemSpriteExtra*) {
            if (action) action();
        });
}

void setLabel(CCMenuItemSpriteExtra* button, std::string const& text) {
    if (auto* face = faceOf(button)) face->setLabel(text);
}

void setIcon(CCMenuItemSpriteExtra* button, char const* frame, float rotation) {
    if (auto* face = faceOf(button)) face->setIcon(frame, rotation);
}

void setTone(CCMenuItemSpriteExtra* button, Tone tone) {
    if (auto* face = faceOf(button)) face->setTone(tone);
}

void setMuted(CCMenuItemSpriteExtra* button, bool muted) {
    if (auto* face = faceOf(button)) face->setMuted(muted);
}

void bounce(CCNode* node) {
    if (!node || !motionOn()) return;
    node->stopActionByTag(kBounceTag);
    float const base = node->getScale();
    auto* pop = CCSequence::create(
        CCEaseSineOut::create(CCScaleTo::create(animTime(0.08f), base * 1.1f)),
        CCEaseBackOut::create(CCScaleTo::create(animTime(0.18f), base)),
        nullptr);
    pop->setTag(kBounceTag);
    node->runAction(pop);
}

SegmentBar* SegmentBar::create(std::vector<Item> items, CCSize size, int touchPriority,
    std::function<void(int)> onSelect, Tone selectedTone) {
    auto* ret = new SegmentBar();
    if (ret->init(std::move(items), size, touchPriority, std::move(onSelect), selectedTone)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SegmentBar::init(std::vector<Item> items, CCSize size, int touchPriority,
    std::function<void(int)> onSelect, Tone selectedTone) {
    if (!CCNode::init() || items.empty()) return false;
    m_onSelect = std::move(onSelect);
    m_selectedTone = selectedTone;
    setContentSize(size);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(size);
    menu->setTouchPriority(touchPriority);
    addChild(menu, 1);

    constexpr float kGap = 3.f;
    float const segment = (size.width - kGap * static_cast<float>(items.size() - 1))
        / static_cast<float>(items.size());
    for (size_t index = 0; index < items.size(); ++index) {
        ButtonSpec spec;
        spec.label = items[index].label;
        spec.width = segment;
        spec.height = size.height;
        spec.fontScale = 0.34f;
        spec.tone = tone::slate;
        auto* button = makeButton(spec, [this, index] {
            int const target = static_cast<int>(index);
            if (target == m_selected) return;
            select(target);
            if (m_onSelect) m_onSelect(target);
        });
        if (!button) continue;
        button->setPosition({
            (segment + kGap) * static_cast<float>(index) + segment / 2.f, size.height / 2.f});
        menu->addChild(button);
        m_buttons.push_back(button);
        if (items[index].dot) {
            if (auto* face = faceOf(button)) face->setDot({90, 95, 110}, false);
        }
    }
    return !m_buttons.empty();
}

void SegmentBar::select(int index, bool animate) {
    if (index < 0 || index >= static_cast<int>(m_buttons.size()) || index == m_selected) return;
    bool const first = m_selected < 0;
    m_selected = index;
    for (size_t slot = 0; slot < m_buttons.size(); ++slot) {
        widgets::setTone(m_buttons[slot],
            static_cast<int>(slot) == index ? m_selectedTone : tone::slate);
    }
    if (animate && !first) bounce(m_buttons[static_cast<size_t>(index)]);
}

void SegmentBar::setLabel(int index, std::string const& text) {
    if (index < 0 || index >= static_cast<int>(m_buttons.size())) return;
    widgets::setLabel(m_buttons[static_cast<size_t>(index)], text);
}

void SegmentBar::setDot(int index, ccColor3B color, bool lit) {
    if (index < 0 || index >= static_cast<int>(m_buttons.size())) return;
    if (auto* face = faceOf(m_buttons[static_cast<size_t>(index)])) face->setDot(color, lit);
}

}
