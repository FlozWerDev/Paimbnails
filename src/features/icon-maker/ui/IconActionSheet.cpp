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

constexpr float kPopupW = 330.f;
constexpr float kRowH = 34.f;

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
    float const listH = static_cast<float>(actions.size()) * (kRowH + 4.f) + 4.f;
    float const popupH = std::clamp(listH + 62.f, 120.f, 280.f);

    if (!Popup::init(kPopupW, popupH)) return false;
    paimon::markDynamicPopup(this);
    setTitle(title.c_str());
    setID("icon-maker-action-sheet"_spr);

    auto size = m_mainLayer->getContentSize();
    float const rowW = size.width - 30.f;

    std::vector<CCNode*> rows;
    rows.reserve(actions.size());
    for (auto& action : actions) {
        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({rowW, kRowH});

        if (auto* panel = kit::makePlate(rowW, kRowH,
                action.destructive ? ui::kAccentDanger : kit::kPlateColor)) {
            panel->setPosition({0.f, 0.f});
            row->addChild(panel, -1);
        }

        auto* label = CCLabelBMFont::create(action.label.c_str(), "bigFont.fnt");
        label->setAnchorPoint({0.f, action.desc.empty() ? 0.5f : 0.f});
        label->setColor(kit::kTitleColor);
        label->limitLabelWidth(rowW - 24.f, 0.42f, 0.15f);
        label->setPosition({12.f, action.desc.empty() ? kRowH / 2.f : kRowH / 2.f + 1.f});
        row->addChild(label);

        if (!action.desc.empty()) {
            auto* desc = CCLabelBMFont::create(action.desc.c_str(), "chatFont.fnt");
            desc->setAnchorPoint({0.f, 1.f});
            desc->setScale(0.4f);
            desc->setColor(kit::kDescColor);
            desc->limitLabelWidth((rowW - 24.f) / 0.4f, 0.4f, 0.16f);
            desc->setPosition({12.f, kRowH / 2.f - 1.f});
            row->addChild(desc);
        }

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(
            CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        row->addChild(menu, 5);

        // Invisible full-width hit area: the whole row is the button.
        auto* hit = CCNode::create();
        hit->setAnchorPoint({0.5f, 0.5f});
        hit->setContentSize({rowW, kRowH});
        Ref<IconActionSheet> self = this;
        auto run = action.run;
        auto* btn = CCMenuItemExt::createSpriteExtra(hit,
            [self, run](CCMenuItemSpriteExtra*) {
                if (self) self->onClose(nullptr);
                // Out of the touch dispatcher: the action may rebuild the scene.
                Loader::get()->queueInMainThread([run] {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (run) run();
                });
            });
        btn->setPosition({rowW / 2.f, kRowH / 2.f});
        menu->addChild(btn);

        rows.push_back(row);
    }

    auto* scroll = kit::makeScrollStack({rowW, size.height - 52.f}, rows, 4.f);
    if (scroll) {
        scroll->setPosition({(size.width - rowW) / 2.f, 10.f});
        m_mainLayer->addChild(scroll);
    }

    return true;
}

}  // namespace paimon::icon_maker
