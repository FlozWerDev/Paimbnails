#include "IconSetNamePopup.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include "../../../ui/PaimonUI.hpp"

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
    if (!PaimonPopup::init(kWidth, kHeight)) return false;

    m_onConfirm = std::move(onConfirm);
    this->setTitle(title.c_str());
    this->setID("icon-set-name-popup"_spr);
    this->addInfoButton("Set Name",
        "Give this icon set a <cy>name</c> so you can find it later. "
        "Letters, numbers, spaces, <co>-</c> and <co>_</c> are allowed.");
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    CCSize insetSize = {content.width - 40.f, 56.f};
    auto inset = paimon::ui::makeInset(insetSize);
    inset->setPosition({cx - insetSize.width / 2.f, content.height / 2.f - 6.f});
    m_mainLayer->addChild(inset);

    auto nameLabel = paimon::ui::makeLabel("Set name", insetSize.width - 20.f, 0.4f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({cx - insetSize.width / 2.f + 2.f, content.height / 2.f + 48.f});
    m_mainLayer->addChild(nameLabel);

    m_input = TextInput::create(insetSize.width - 24.f, "Set name");
    if (m_input) {
        m_input->setPosition({cx, content.height / 2.f + 22.f});
        m_input->setMaxCharCount(24);
        m_input->setFilter(kAllowed);
        m_input->setString(initial);
        m_mainLayer->addChild(m_input, 1);
    }

    WeakRef<IconSetNamePopup> self = this;
    auto* btn = paimon::ui::makeButton("Save", [self] {
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
    }, paimon::ui::Btn::Green, 0.f, 0.7f);
    btn->setID("save-name-button"_spr);
    btn->setPosition({cx, 26.f});
    m_buttonMenu->addChild(btn);

    return true;
}

void IconSetNamePopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_input);
    m_input = nullptr;
    Popup::onClose(sender);
}

}  // namespace paimon::iconcopy
