#include "SameAsPickerPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../services/LayerBackgroundManager.hpp"
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

SameAsPickerPopup* SameAsPickerPopup::create(std::string const& currentKey, geode::CopyableFunction<void(std::string const&)> onPick) {
    auto ret = new SameAsPickerPopup();
    if (ret && ret->init(currentKey, std::move(onPick))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SameAsPickerPopup::init(std::string const& currentKey, geode::CopyableFunction<void(std::string const&)> onPick) {
    if (!PaimonPopup::init(240.f, 275.f)) return false;

    m_selectedLayerKey = currentKey;
    m_onPick = std::move(onPick);

    this->setTitle("Same as...");
    this->addInfoButton("Same as...",
        "Pick another layer to <cy>mirror its background</c>. This layer will reuse "
        "whatever that layer shows, so changing the source updates both.");

    auto content = m_mainLayer->getContentSize();

    struct Opt { std::string key; std::string label; };
    std::vector<Opt> options;
    for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (k == m_selectedLayerKey) continue;
        options.push_back({k, n});
    }

    CCSize panelSize = {content.width - 24.f, content.height - 58.f};
    auto* panel = paimon::ui::makePanel(panelSize, "Mirror Layer");
    panel->setPosition({content.width / 2.f - panelSize.width / 2.f, 14.f});
    m_mainLayer->addChild(panel, 1);

    auto menu = CCMenu::create();
    menu->setID("same-as-picker-menu"_spr);
    menu->setContentSize({panelSize.width - 16.f, panelSize.height - paimon::ui::kPanelHeader - 12.f});
    menu->setLayout(
        ColumnLayout::create()
            ->setGap(6.f)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAxisReverse(true)
            ->setDefaultScaleLimits(0.5f, 1.f)
    );
    panel->addChild(menu);
    menu->setPosition({panelSize.width / 2.f, (panelSize.height - paimon::ui::kPanelHeader) / 2.f});
    menu->setZOrder(10);

    for (int i = 0; i < (int)options.size(); i++) {
        auto& opt = options[i];
        auto spr = ButtonSprite::create(opt.label.c_str(), 108, true, "bigFont.fnt", "GJ_button_05.png", 18.f, 0.45f);

        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, optKey = opt.key](CCMenuItemSpriteExtra*) {
            if (m_onPick) m_onPick(optKey);
            this->onClose(nullptr);
        });
        btn->setID(fmt::format("{}/same-as-{}-btn", Mod::get()->getID(), opt.key));
        menu->addChild(btn);
    }

    menu->updateLayout();

    paimon::markDynamicPopup(this);
    return true;
}

