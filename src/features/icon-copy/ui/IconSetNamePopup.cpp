#include "IconSetNamePopup.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr float kWidth = 320.f;
constexpr float kHeight = 150.f;
constexpr char const* kAllowed =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";

std::string trimmed(std::string text) {
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

}  // anonymous namespace

IconSetNamePopup* IconSetNamePopup::create(std::string title, std::string initial,
                                           Callback onConfirm) {
    auto* popup = new IconSetNamePopup();
    if (popup->init(title, initial, std::move(onConfirm))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool IconSetNamePopup::init(std::string const& title, std::string const& initial,
                            Callback onConfirm) {
    if (!Popup::init(kWidth, kHeight)) return false;

    m_onConfirm = std::move(onConfirm);
    this->setTitle(title.c_str());
    this->setID("icon-set-name-popup"_spr);
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();

    m_input = TextInput::create(content.width - 60.f, "Set name");
    if (m_input) {
        m_input->setPosition({content.width / 2.f, content.height / 2.f});
        m_input->setMaxCharCount(24);
        m_input->setFilter(kAllowed);
        m_input->setString(initial);
        m_mainLayer->addChild(m_input);
    }

    if (auto* spr = ButtonSprite::create("Save", "bigFont.fnt", "GJ_button_01.png", 0.6f)) {
        WeakRef<IconSetNamePopup> self = this;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr, [self](CCMenuItemSpriteExtra*) {
            auto popup = self.lock();
            if (!popup) return;

            auto const name = trimmed(popup->m_input ? popup->m_input->getString() : "");
            if (name.empty()) {
                PaimonNotify::show("Give the set a name", NotificationIcon::Warning);
                return;
            }

            auto callback = popup->m_onConfirm;
            popup->onClose(nullptr);
            Loader::get()->queueInMainThread([callback, name] {
                if (paimon::isRuntimeShuttingDown()) return;
                if (callback) callback(name);
            });
        });
        btn->setID("save-name-button"_spr);
        btn->setPosition({content.width / 2.f, 26.f});
        m_buttonMenu->addChild(btn);
    }

    return true;
}

void IconSetNamePopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_input);
    m_input = nullptr;
    Popup::onClose(sender);
}

}  // namespace paimon::iconcopy
