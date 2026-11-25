#include "CopyIconsPopup.hpp"

#include "CopyChoicePopup.hpp"
#include "IconDetailPopup.hpp"
#include "IconSetPreview.hpp"
#include "../hooks/IconCopyGarageGlue.hpp"
#include "../../community-base/services/CommunityClient.hpp"
#include "../../global-icon/GlobalIconGradients.hpp"
#include "../../global-icon/services/GlobalIconClient.hpp"
#include "../../global-icon/services/GlobalIconStorage.hpp"
#include "../../../framework/compat/MoreIconsAPI.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"

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

// strip: the touch box is wider than the drawing so neighbours stay tappable.
constexpr float kSlot = 32.f;
constexpr float kSlotGap = 2.f;
constexpr float kIconBox = 26.f;

// one dot per color the set carries, so you can tell two similar icon sets
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
    if (!PaimonPopup::init(kWidth, kHeight)) return false;

    m_set = set;
    this->setTitle(saved ? "Icon Set" : "Copy Icons");
    this->setID("copy-icons-popup"_spr);
    this->addInfoButton(saved ? "Icon Set" : "Copy Icons",
        "Preview every gamemode icon in this set and its colors. "
        "Tap an icon to see <cy>how it unlocks</c>. <cg>Copy</c> saves the set, "
        "<cg>Use</c> wears it now.");
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;
    float const plateX = cx - kPlateWidth / 2.f;

    // header card: who the set belongs to, plus its colors.
    if (auto* plate = paimon::ui::makeInset({kPlateWidth, 34.f})) {
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

    // the set itself, framed so it reads as one thing instead of loose icons.
    if (auto* plate = paimon::ui::makeInset({kPlateWidth, 46.f})) {
        plate->setPosition({plateX, 112.f});
        m_mainLayer->addChild(plate);
    }

    buildStrip(cx, 135.f);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);

    struct Action {
        char const* text;
        paimon::ui::Btn skin;
        cocos2d::SEL_MenuHandler handler;
        std::string id;
        bool copies;
    };
    Action const actions[] = {
        {"Copy & Use", paimon::ui::Btn::Green,
         menu_selector(CopyIconsPopup::onCopyAndUse), "copy-and-use-button"_spr, true},
        {"Copy", paimon::ui::Btn::Cyan,
         menu_selector(CopyIconsPopup::onCopy), "copy-button"_spr, true},
        {"Use", paimon::ui::Btn::Blue,
         menu_selector(CopyIconsPopup::onUse), "use-button"_spr, false},
    };

    // same width for all three; buttonsprite rounds width up, so lay out from the actual size.
    std::vector<CCMenuItemSpriteExtra*> buttons;
    float total = 0.f;
    for (auto const& action : actions) {
        if (saved && action.copies) continue;
        auto* spr = paimon::ui::makeButtonSprite(action.text, action.skin, kButtonWidth, kButtonScale, "bigFont.fnt");
        if (!spr) continue;
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
    m_hint = hint;

    // the player's global icon may also share gradients and custom textures.
    if (!saved && m_set.accountID > 0) {
        WeakRef<CopyIconsPopup> weak = this;
        paimon::globalicon::GlobalIconClient::get().getMetadata(m_set.accountID,
            [weak](bool ok, bool found, paimon::globalicon::GlobalIconMeta const& meta) {
                auto self = weak.lock();
                if (!self) return;
                self->m_metaLoaded = ok;
                if (!ok || !found) return;
                self->m_meta = meta;
                auto available = self->extras();
                if (!available.gradients && !available.custom) return;
                std::string text = paimon::community::tx("Tambien comparte: ", "Also shares: ");
                if (available.gradients) text += paimon::community::tx("gradientes", "gradients");
                if (available.gradients && available.custom) text += ", ";
                if (available.custom) text += paimon::community::tx("iconos custom", "custom icons");
                self->m_hint->setString(text.c_str());
                self->m_hint->setColor({130, 255, 170});
            });
    }

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

    // read the icon's spot on screen before the touch handler tears anything
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

paimon::globalicon::LinkOptions CopyIconsPopup::extras() const {
    return {true,
        m_meta.share.gradients && m_meta.gradients.isObject() && m_meta.gradients.size() > 0,
        m_meta.share.custom && !m_meta.icons.empty() && paimon::globalicon::GlobalIconStorage::available()};
}

void CopyIconsPopup::choose(Mode mode) {
    auto available = extras();
    if (!available.gradients && !available.custom) {
        perform(mode, {true, false, false});
        return;
    }
    char const* action = mode == Mode::Copy ? "Copy" : mode == Mode::Use ? "Use" : "Copy & Use";
    WeakRef<CopyIconsPopup> weak = this;
    auto* popup = CopyChoicePopup::create(available, action, [weak, mode](paimon::globalicon::LinkOptions options) {
        if (auto self = weak.lock()) self->perform(mode, options);
    });
    paimon::community::showOnTop(popup);
}

void CopyIconsPopup::perform(Mode mode, paimon::globalicon::LinkOptions options) {
    bool const keep = mode != Mode::Use;
    bool const wear = mode != Mode::Copy;
    if (options.vanilla) {
        if (keep) iconcopy::add(m_set);
        if (wear) iconcopy::apply(m_set);
    }
    // async parts only capture copies: this popup closes right away.
    auto meta = m_meta;
    if (options.gradients) {
        paimon::globalicon::ensureGradientImages(meta, [meta, wear](paimon::globalicon::GradientPaths const& paths) {
            int written = paimon::globalicon::copyGradients(meta, paths, wear);
            garage::refreshVisibleGarage();
            PaimonNotify::show(fmt::format("{} {}", written, wear
                ? paimon::community::tx("gradientes aplicados", "gradients applied")
                : paimon::community::tx("gradientes guardados en tu biblioteca", "gradients saved to your library")),
                written > 0 ? NotificationIcon::Success : NotificationIcon::Warning);
        });
    }
    if (options.custom) {
        std::vector<paimon::globalicon::GlobalIconSlot> slots;
        for (auto const& [type, slot] : meta.icons) slots.push_back(slot);
        int accountID = meta.accountID;
        paimon::globalicon::GlobalIconStorage::get().ensureIcons(accountID, slots,
            [accountID, slots, wear](int succeeded, int total) {
                if (wear) {
                    for (auto const& slot : slots) {
                        auto type = paimon::globalicon::iconTypeFromString(slot.type);
                        if (!type) continue;
                        auto* info = more_icons::getIcon(paimon::globalicon::GlobalIconStorage::registeredName(accountID, slot), *type);
                        if (info) more_icons::setIcon(info, *type);
                    }
                    garage::refreshVisibleGarage();
                }
                PaimonNotify::show(fmt::format("{} / {} {}", succeeded, total,
                    paimon::community::tx("iconos custom descargados", "custom icons downloaded")),
                    succeeded > 0 ? NotificationIcon::Success : NotificationIcon::Error);
            });
    }
    if (options.vanilla) {
        if (wear) garage::refreshVisibleGarage();
        PaimonNotify::show(wear ? fmt::format("Now using {}'s icons", m_set.label()) : fmt::format("Saved {}'s icons", m_set.label()),
            NotificationIcon::Success);
    }
    this->onClose(nullptr);
}

void CopyIconsPopup::onCopyAndUse(CCObject*) { choose(Mode::CopyAndUse); }

void CopyIconsPopup::onCopy(CCObject*) { choose(Mode::Copy); }

void CopyIconsPopup::onUse(CCObject*) { choose(Mode::Use); }

}  // namespace paimon::iconcopy
