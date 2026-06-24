#include "SameAsPickerPopup.hpp"
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
    if (!Popup::init(240.f, 275.f)) return false;

    m_selectedLayerKey = currentKey;
    m_onPick = std::move(onPick);

    this->setTitle("Same as...");

    auto content = m_mainLayer->getContentSize();

    // options: all layers except the selected one
    struct Opt { std::string key; std::string label; };
    std::vector<Opt> options;
    for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (k == m_selectedLayerKey) continue;
        options.push_back({k, n});
    }

    auto menu = CCMenu::create();
    menu->setID("same-as-picker-menu"_spr);
    menu->setContentSize({content.width - 20.f, content.height - 60.f});
    menu->setLayout(
        ColumnLayout::create()
            ->setGap(6.f)
            ->setAxisAlignment(AxisAlignment::Start)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAxisReverse(true)
    );
    m_mainLayer->addChildAtPosition(menu, Anchor::Center, {0.f, -10.f});
    menu->setZOrder(10);

    for (int i = 0; i < (int)options.size(); i++) {
        auto& opt = options[i];
        auto spr = ButtonSprite::create(opt.label.c_str(), "bigFont.fnt", "GJ_button_01.png", .7f);
        spr->setScale(0.52f);

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

