#include "CopyIconsPopup.hpp"

#include "IconDetailPopup.hpp"
#include "IconSetPreview.hpp"
#include "../hooks/IconCopyGarageGlue.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/GameManager.hpp>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr float kWidth = 380.f;
constexpr float kHeight = 250.f;
constexpr float kPlateWidth = 340.f;
constexpr float kButtonWidth = 104.f;
constexpr float kButtonScale = 0.9f;
constexpr float kButtonGap = 10.f;

// Strip: the touch box is wider than the drawing so neighbours stay tappable.
constexpr float kSlot = 32.f;
constexpr float kSlotGap = 2.f;
constexpr float kIconBox = 26.f;

// One dot per color the set carries, so you can tell two similar icon sets
// apart before applying one.
CCNode* makeColorDots(IconSet const& set) {
    auto* gm = GameManager::get();
    if (!gm) return nullptr;

    std::vector<ccColor3B> colors{gm->colorForIdx(set.color1), gm->colorForIdx(set.color2)};
    if (set.glow) {
        colors.push_back(set.glowColor >= 0 ? gm->colorForIdx(set.glowColor) : colors[1]);
    }

    constexpr float radius = 5.f;
    constexpr float step = 15.f;
    auto* row = CCNode::create();
    float x = 0.f;
    for (auto const& color : colors) {
        auto* dot = PaimonDrawNode::create();
        if (!dot) continue;
        dot->drawSolidCircle({radius, radius}, radius,
            ccc4f(color.r / 255.f, color.g / 255.f, color.b / 255.f, 1.f));
        dot->setContentSize({radius * 2.f, radius * 2.f});
        dot->setPosition({x, 0.f});
        row->addChild(dot);
        x += step;
    }
    row->setContentSize({std::max(x - step + radius * 2.f, 0.f), radius * 2.f});
    return row;
}

}  // anonymous namespace

CopyIconsPopup* CopyIconsPopup::create(IconSet const& set, bool saved) {
    auto* popup = new CopyIconsPopup();
    if (popup->init(set, saved)) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool CopyIconsPopup::init(IconSet const& set, bool saved) {
    if (!Popup::init(kWidth, kHeight)) return false;

    m_set = set;
    this->setTitle(saved ? "Icon Set" : "Copy Icons");
    this->setID("copy-icons-popup"_spr);
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;
    float const plateX = cx - kPlateWidth / 2.f;

    // Header card: who the set belongs to, plus its colors.
    if (auto* plate = paimon::SpriteHelper::createDarkPanel(kPlateWidth, 34.f, 90, 6.f)) {
        plate->setPosition({plateX, 176.f});
        m_mainLayer->addChild(plate);
    }

    auto* name = CCLabelBMFont::create(m_set.label().c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->limitLabelWidth(kPlateWidth - 90.f, 0.55f, 0.2f);
    name->setPosition({plateX + 14.f, 193.f});
    m_mainLayer->addChild(name);

    if (auto* dots = makeColorDots(m_set)) {
        dots->setPosition({plateX + kPlateWidth - 14.f - dots->getContentWidth(), 188.f});
        m_mainLayer->addChild(dots);
    }

    // The set itself, framed so it reads as one thing instead of loose icons.
    if (auto* plate = paimon::SpriteHelper::createDarkPanel(kPlateWidth, 46.f, 90, 6.f)) {
        plate->setPosition({plateX, 112.f});
        m_mainLayer->addChild(plate);
    }

    buildStrip(cx, 135.f);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);

    struct Action {
        char const* text;
        char const* sprite;
        cocos2d::SEL_MenuHandler handler;
        std::string id;
        bool copies;
    };
    Action const actions[] = {
        {"Copy & Use", "GJ_button_01.png",
         menu_selector(CopyIconsPopup::onCopyAndUse), "copy-and-use-button"_spr, true},
        {"Copy", "GJ_button_04.png",
         menu_selector(CopyIconsPopup::onCopy), "copy-button"_spr, true},
        {"Use", "GJ_button_02.png",
         menu_selector(CopyIconsPopup::onUse), "use-button"_spr, false},
    };

    // Same width for the three so the row doesn't look accidental. ButtonSprite
    // rounds the absolute width up, so lay them out from the size it actually
    // came out as instead of the requested one.
    std::vector<CCMenuItemSpriteExtra*> buttons;
    float total = 0.f;
    for (auto const& action : actions) {
        if (saved && action.copies) continue;
        auto* spr = ButtonSprite::create(action.text, static_cast<int>(kButtonWidth), true,
                                         "bigFont.fnt", action.sprite, 30.f, 0.5f);
        if (!spr) continue;
        spr->setScale(kButtonScale);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, action.handler);
        btn->setID(action.id);
        menu->addChild(btn);
        buttons.push_back(btn);
        total += spr->getScaledContentSize().width + (buttons.size() > 1 ? kButtonGap : 0.f);
    }

    float x = cx - total / 2.f;
    for (auto* btn : buttons) {
        float const w = btn->getScaledContentSize().width;
        btn->setPosition({x + w / 2.f, 64.f});
        x += w + kButtonGap;
    }

    auto* hint = CCLabelBMFont::create("Tap an icon to see how it unlocks", "chatFont.fnt");
    hint->setScale(0.42f);
    hint->setColor({160, 160, 180});
    hint->setPosition({cx, 26.f});
    m_mainLayer->addChild(hint);

    return true;
}

void CopyIconsPopup::buildStrip(float centreX, float centreY) {
    auto const modes = previewGamemodes();
    if (modes.empty()) return;

    float const count = static_cast<float>(modes.size());
    float const left = centreX - (count * kSlot + (count - 1.f) * kSlotGap) / 2.f;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 1);

    for (std::size_t index = 0; index < modes.size(); ++index) {
        auto const type = modes[index];

        auto* face = CCNode::create();
        face->setContentSize({kSlot, kSlot});
        face->setAnchorPoint({0.5f, 0.5f});
        face->ignoreAnchorPointForPosition(false);
        if (auto* preview = makePreview(m_set, type, kIconBox)) {
            preview->setPosition({kSlot / 2.f, kSlot / 2.f});
            face->addChild(preview);
        }

        auto* slot = CCMenuItemExt::createSpriteExtra(face,
            [this, type](CCMenuItemSpriteExtra* sender) { this->openDetail(type, sender); });
        if (!slot) continue;

        slot->setID(fmt::format("{}/gamemode-slot-{}", Mod::get()->getID(), index));
        slot->setPosition(
            {left + kSlot / 2.f + static_cast<float>(index) * (kSlot + kSlotGap), centreY});
        menu->addChild(slot);
    }
}

void CopyIconsPopup::openDetail(IconType type, CCNode* slot) {
    if (!slot || !slot->getParent()) return;

    // Read the icon's spot on screen before the touch handler tears anything
    // down: the mod's popup entrance animation grows the card out of it.
    auto const origin = slot->getParent()->convertToWorldSpace(slot->getPosition());
    auto const set = m_set;
    Loader::get()->queueInMainThread([set, type, origin] {
        if (paimon::isRuntimeShuttingDown()) return;
        auto* popup = IconDetailPopup::create(set, type);
        if (!popup) return;
        paimon::storeButtonOrigin(origin);
        popup->show();
    });
}

void CopyIconsPopup::onCopyAndUse(CCObject*) {
    iconcopy::add(m_set);
    iconcopy::apply(m_set);
    garage::refreshVisibleGarage();
    PaimonNotify::show(fmt::format("Copied and using {}'s icons", m_set.label()),
                       NotificationIcon::Success);
    this->onClose(nullptr);
}

void CopyIconsPopup::onCopy(CCObject*) {
    iconcopy::add(m_set);
    PaimonNotify::show(fmt::format("Saved {}'s icons", m_set.label()),
                       NotificationIcon::Success);
    this->onClose(nullptr);
}

void CopyIconsPopup::onUse(CCObject*) {
    iconcopy::apply(m_set);
    garage::refreshVisibleGarage();
    PaimonNotify::show(fmt::format("Now using {}'s icons", m_set.label()),
                       NotificationIcon::Success);
    this->onClose(nullptr);
}

}  // namespace paimon::iconcopy
