#include "VersusHandNode.hpp"
#include "VersusCardNode.hpp"
#include "../services/VersusEffects.hpp"
#include "../services/VersusSession.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::versus {

namespace {

constexpr float kCardW = 46.f;
constexpr float kSpyCardW = 30.f;
constexpr float kSlotGap = 54.f;
constexpr float kSpyGap = 36.f;
constexpr float kEffectSize = 22.f;

// The colour is the owner's, so a card that changes hands is a different glyph.
bool sameEffects(std::vector<ActiveEffect> const& active, std::vector<ActiveEffect> const& drawn) {
    return std::equal(active.begin(), active.end(), drawn.begin(), drawn.end(),
        [](ActiveEffect const& a, ActiveEffect const& b) {
            return a.card == b.card && a.fromRival == b.fromRival;
        });
}

} // namespace

VersusHandNode* VersusHandNode::create() {
    auto ret = new VersusHandNode();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool VersusHandNode::init() {
    if (!CCNode::init()) return false;

    auto const winSize = CCDirector::get()->getWinSize();
    this->setPosition({0.f, 0.f});

    m_slots = CCNode::create();
    m_slots->setPosition({winSize.width - 46.f, 52.f});
    this->addChild(m_slots, 1);

    // Under the rival's bar, so what the Eye shows sits next to whose it is.
    m_rivalSlots = CCNode::create();
    m_rivalSlots->setPosition({winSize.width - 34.f, winSize.height - 78.f});
    m_rivalSlots->setVisible(false);
    this->addChild(m_rivalSlots, 1);

    m_effects = CCNode::create();
    m_effects->setPosition({winSize.width - 16.f, 110.f});
    this->addChild(m_effects, 1);

    m_locked = CCLabelBMFont::create(
        Localization::get().getString("versus.hand.locked").c_str(), "chatFont.fnt");
    m_locked->setScale(0.4f);
    m_locked->setAnchorPoint({1.f, 0.5f});
    m_locked->setColor({240, 130, 140});
    m_locked->setPosition({winSize.width - 12.f, 24.f});
    m_locked->setVisible(false);
    this->addChild(m_locked, 2);

    this->scheduleUpdate();
    refresh();
    return true;
}

void VersusHandNode::update(float) {
    refresh();
}

void VersusHandNode::refresh() {
    auto const& session = VersusSession::get();
    auto const& effects = VersusEffects::get();

    auto const& hand = session.hand();
    if (hand != m_drawn) {
        m_drawn = hand;
        rebuildHand();
    }

    // Their hand is only ours to look at while the Eye is up.
    bool const spying = effects.seesRival();
    m_rivalSlots->setVisible(spying);
    if (spying && session.rivalHand() != m_drawnRival) {
        m_drawnRival = session.rivalHand();
        rebuildRivalHand();
    }

    auto const& active = effects.active();
    if (!sameEffects(active, m_drawnEffects)) {
        m_drawnEffects = active;
        rebuildEffects(active);
    }

    for (size_t i = 0; i < m_rings.size() && i < active.size(); i++) {
        if (!m_rings[i] || active[i].total <= 0.f) continue;
        m_rings[i]->setPercentage(
            std::clamp(active[i].remaining / active[i].total, 0.f, 1.f) * 100.f);
    }

    m_locked->setVisible(effects.cardsLocked());
}

void VersusHandNode::rebuildHand() {
    m_slots->removeAllChildren();

    // Touch menu wrapping the cards: on mobile there are no Q/E keybinds,
    // so tapping the card itself is the only way to play it. On desktop the
    // keybinds keep working and the tap is an extra path.
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setID("versus-hand-touch-menu"_spr);
    m_slots->addChild(menu, 30);

    for (size_t i = 0; i < m_drawn.size(); i++) {
        auto* card = VersusCardNode::create(m_drawn[i], kCardW);
        if (!card) continue;
        card->setPosition({-static_cast<float>(i) * kSlotGap, 0.f});
        card->playDraw(0.f);

        auto* item = CCMenuItemSpriteExtra::create(
            card, this, menu_selector(VersusHandNode::onPlayCard));
        item->setTag(static_cast<int>(i));
        item->setPosition(card->getPosition());
        menu->addChild(item);

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
        if (i == 0) {
            auto* hint = CCLabelBMFont::create(
                Localization::get().getString("versus.hand.tap").c_str(), "bigFont.fnt");
            hint->setScale(0.30f);
            hint->setOpacity(150);
            hint->setPosition({-kSlotGap / 2.f, -kCardW * 0.86f - 10.f});
            m_slots->addChild(hint, 20);
        }
#else
        auto* key = CCLabelBMFont::create(i == 0 ? "Q" : "E", "bigFont.fnt");
        key->setScale(0.34f);
        key->setOpacity(190);
        key->setPosition({-static_cast<float>(i) * kSlotGap, -kCardW * 0.86f});
        m_slots->addChild(key, 20);
#endif
    }
}

void VersusHandNode::onPlayCard(CCObject* sender) {
    auto* item = geode::cast::typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!item) return;
    VersusSession::get().playCard(item->getTag());
}

void VersusHandNode::rebuildRivalHand() {
    m_rivalSlots->removeAllChildren();

    for (size_t i = 0; i < m_drawnRival.size(); i++) {
        auto* card = VersusCardNode::create(m_drawnRival[i], kSpyCardW);
        if (!card) continue;
        card->setPosition({-static_cast<float>(i) * kSpyGap, 0.f});
        m_rivalSlots->addChild(card, static_cast<int>(10 - i));
    }
}

void VersusHandNode::rebuildEffects(std::vector<ActiveEffect> const& active) {
    m_effects->removeAllChildren();
    m_rings.clear();

    for (size_t i = 0; i < active.size(); i++) {
        auto const& effect = active[i];
        auto const& def = cardAt(effect.card);
        m_rings.push_back(nullptr);

        auto* glyph = paimon::SpriteHelper::safeCreate(cardGlyphSprite(def).c_str());
        if (!glyph) continue;
        glyph->setScale(kEffectSize / std::max(1.f, glyph->getContentSize().width));
        glyph->setPosition({0.f, static_cast<float>(i) * (kEffectSize + 6.f)});
        // Incoming cards are the rival's doing, so they read in his colour.
        glyph->setColor(effect.fromRival ? ccColor3B{240, 120, 140} : ccColor3B{140, 220, 250});
        m_effects->addChild(glyph, 1);

        if (effect.total <= 0.f) continue;

        auto* pip = paimon::SpriteHelper::safeCreate("paim_vsPip.png"_spr);
        if (!pip) continue;

        auto* ring = CCProgressTimer::create(pip);
        if (!ring) continue;
        ring->setType(kCCProgressTimerTypeRadial);
        ring->setReverseDirection(true);
        ring->setScale(kEffectSize * 1.5f / std::max(1.f, ring->getContentSize().width));
        ring->setPosition(glyph->getPosition());
        ring->setOpacity(120);
        m_effects->addChild(ring, 0);
        m_rings.back() = ring;
    }
}

} // namespace paimon::versus
