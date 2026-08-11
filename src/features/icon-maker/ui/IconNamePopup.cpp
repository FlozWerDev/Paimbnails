#include "IconNamePopup.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 320.f;
constexpr float kPopupH = 150.f;
constexpr char const* kAllowed =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";

}  // anonymous namespace

IconNamePopup* IconNamePopup::create(std::string title, std::string placeholder,
                                     std::string initial, ConfirmCallback onConfirm) {
    auto* p = new IconNamePopup();
    if (p->init(std::move(title), std::move(placeholder), std::move(initial),
                std::move(onConfirm))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool IconNamePopup::init(std::string title, std::string placeholder,
                         std::string initial, ConfirmCallback onConfirm) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    m_onConfirm = std::move(onConfirm);
    setTitle(title.c_str());
    setID("icon-maker-name-popup"_spr);

    auto size = m_mainLayer->getContentSize();

    m_input = TextInput::create(size.width - 60.f, placeholder.c_str());
    if (m_input) {
        m_input->setPosition({size.width / 2.f, size.height / 2.f});
        m_input->setMaxCharCount(24);
        m_input->setFilter(kAllowed);
        m_input->setString(initial);
        m_mainLayer->addChild(m_input);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);

    if (auto* spr = ButtonSprite::create("Guardar", "bigFont.fnt", "GJ_button_01.png", 0.6f)) {
        Ref<IconNamePopup> self = this;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr, [self](CCMenuItemSpriteExtra*) {
            if (!self) return;
            std::string value = self->m_input ? self->m_input->getString() : "";
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            while (!value.empty() && value.back() == ' ') value.pop_back();
            if (value.empty()) {
                Notification::create("Escribe un nombre.", NotificationIcon::Warning, 2.f)->show();
                return;
            }
            auto callback = self->m_onConfirm;
            self->onClose(nullptr);
            Loader::get()->queueInMainThread([callback, value] {
                if (paimon::isRuntimeShuttingDown()) return;
                if (callback) callback(value);
            });
        });
        btn->setPosition({size.width / 2.f, 26.f});
        menu->addChild(btn);
    }

    return true;
}

}  // namespace paimon::icon_maker
