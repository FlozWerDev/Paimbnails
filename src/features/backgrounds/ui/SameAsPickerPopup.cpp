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
    if (!Popup::init(220.f, 230.f)) return false;

    m_selectedLayerKey = currentKey;
    m_onPick = std::move(onPick);

    this->setTitle("Same as...");

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // opciones: todos los layers excepto el seleccionado
    struct Opt { std::string key; std::string label; };
    std::vector<Opt> options;
    for (auto& [k, n] : LayerBackgroundManager::LAYER_OPTIONS) {
        if (k == m_selectedLayerKey) continue;
        options.push_back({k, n});
    }

    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    float startY = content.height - 55.f;
    float spacing = 34.f;

    for (int i = 0; i < (int)options.size(); i++) {
        auto& opt = options[i];
        auto spr = ButtonSprite::create(opt.label.c_str(), "bigFont.fnt", "GJ_button_01.png", .7f);
        spr->setScale(0.6f);

        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, optKey = opt.key](CCMenuItemSpriteExtra*) {
            if (m_onPick) m_onPick(optKey);
            this->onClose(nullptr);
        });
        btn->setID(fmt::format("{}/same-as-{}-btn", Mod::get()->getID(), opt.key));
        btn->setPosition({cx, startY - spacing * i});
        menu->addChild(btn);
    }

    paimon::markDynamicPopup(this);
    return true;
}

