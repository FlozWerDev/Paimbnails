#include "IconActionSheet.hpp"

#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>

#include <algorithm>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 300.f;
constexpr float kRowH = 30.f;
constexpr float kPlainRowH = 24.f;
constexpr float kRowGap = 3.f;

}  // anonymous namespace

IconActionSheet* IconActionSheet::create(std::string title, std::vector<Action> actions) {
    auto* p = new IconActionSheet();
    if (p->init(std::move(title), std::move(actions))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool IconActionSheet::init(std::string title, std::vector<Action> actions) {
    float listH = 4.f;
    for (auto const& action : actions) {
        listH += (action.desc.empty() ? kPlainRowH : kRowH) + kRowGap;
    }
    float const popupH = std::clamp(listH + 52.f, 110.f, 280.f);

    if (!PaimonPopup::init(kPopupW, popupH)) return false;
    paimon::markDynamicPopup(this);
    setTitle(title.c_str(), "goldFont.fnt", 0.6f, 18.f);
    setID("icon-maker-action-sheet"_spr);

    auto size = m_mainLayer->getContentSize();
    float const rowW = size.width - 26.f;

    std::vector<CCNode*> rows;
    rows.reserve(actions.size());
    for (auto& action : actions) {
        bool const plain = action.desc.empty();
        float const rowH = plain ? kPlainRowH : kRowH;
        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({rowW, rowH});

        if (auto* panel = kit::makePlate(rowW, rowH,
                action.destructive ? ui::kAccentDanger : kit::kPlateColor)) {
            panel->setPosition({0.f, 0.f});
            row->addChild(panel, -1);
        }

        // reads as tappable without drawing a button inside a button.
        if (auto* chevron = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png")) {
            chevron->setFlipX(true);
            chevron->setScale(0.28f);
            chevron->setOpacity(150);
            chevron->setPosition({rowW - 12.f, rowH / 2.f});
            row->addChild(chevron);
        }

        auto* label = CCLabelBMFont::create(action.label.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.f, plain ? 0.5f : 0.f});
        label->setColor(kit::kTitleColor);
        label->limitLabelWidth(rowW - 34.f, 0.34f, 0.15f);
        label->setPosition({10.f, plain ? rowH / 2.f : rowH / 2.f + 1.f});
        row->addChild(label);

        if (!plain) {
            auto* desc = CCLabelBMFont::create(action.desc.c_str(), "chatFont.fnt");
            desc->setAnchorPoint({0.f, 1.f});
            desc->setColor(kit::kDescColor);
            desc->limitLabelWidth(rowW - 34.f, 0.38f, 0.16f);
            desc->setPosition({10.f, rowH / 2.f - 1.f});
            row->addChild(desc);
        }

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(
            CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        row->addChild(menu, 5);

        // invisible full-width hit area: the whole row is the button.
        auto* hit = CCNode::create();
        hit->setAnchorPoint({0.5f, 0.5f});
        hit->setContentSize({rowW, rowH});
        Ref<IconActionSheet> self = this;
        auto run = action.run;
        auto* btn = CCMenuItemExt::createSpriteExtra(hit,
            [self, run](CCMenuItemSpriteExtra*) {
                if (self) self->onClose(nullptr);
                // out of the touch dispatcher: the action may rebuild the scene.
                Loader::get()->queueInMainThread([run] {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (run) run();
                });
            });
        btn->setPosition({rowW / 2.f, rowH / 2.f});
        menu->addChild(btn);

        rows.push_back(row);
    }

    auto* scroll = kit::makeScrollStack({rowW, size.height - 46.f}, rows, kRowGap);
    if (scroll) {
        scroll->setPosition({(size.width - rowW) / 2.f, 9.f});
        m_mainLayer->addChild(scroll);
    }

    return true;
}

}  // namespace paimon::icon_maker
