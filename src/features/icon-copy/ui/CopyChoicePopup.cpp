#include "CopyChoicePopup.hpp"
#include "../../community-base/services/CommunityClient.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <memory>

using namespace geode::prelude;

namespace paimon::iconcopy {

using paimon::community::tx;

CopyChoicePopup* CopyChoicePopup::create(paimon::globalicon::LinkOptions available, char const* action, Callback callback) {
    auto popup = std::make_unique<CopyChoicePopup>();
    if (!popup->init(available, action, std::move(callback))) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool CopyChoicePopup::init(paimon::globalicon::LinkOptions available, char const* action, Callback callback) {
    constexpr float W = 340.f, H = 240.f;
    if (!PaimonPopup::init(W, H)) return false;
    m_callback = std::move(callback);
    m_options = available;
    m_options.vanilla = true;
    setTitle(tx("Que quieres copiar?", "What do you want to copy?"), "goldFont.fnt", 0.6f);
    paimon::markDynamicPopup(this);

    struct RowSpec {
        char const* title;
        char const* desc;
        bool available;
        bool* value;
    };
    RowSpec rows[] = {
        {tx("Iconos vanilla", "Vanilla icons"), tx("Ids y colores de GD.", "GD icon ids and colors."), true, &m_options.vanilla},
        {tx("Gradientes", "Gradients"), available.gradients ? tx("Sus Icon Gradients, con imagenes.", "Their Icon Gradients, with images.")
            : tx("No comparte gradientes.", "Not sharing gradients."), available.gradients, &m_options.gradients},
        {tx("Iconos custom", "Custom icons"), available.custom ? tx("Sus texturas de More Icons.", "Their More Icons textures.")
            : tx("No comparte iconos custom o falta More Icons.", "Not sharing custom icons, or More Icons is missing."),
            available.custom, &m_options.custom},
    };
    float y = H - 64.f;
    for (auto const& spec : rows) {
        CCMenuItemToggler* toggle = nullptr;
        auto* value = spec.value;
        auto* row = paimon::configkit::makeToggleRow(W - 40.f, spec.title, spec.desc, *value && spec.available,
            [value](bool on) { *value = on; }, &toggle);
        row->ignoreAnchorPointForPosition(false);
        row->setAnchorPoint({0.5f, 0.5f});
        row->setPosition({W / 2.f, y});
        m_mainLayer->addChild(row);
        if (!spec.available && toggle) {
            *value = false;
            toggle->setEnabled(false);
            toggle->setOpacity(110);
        }
        y -= row->getContentSize().height + 6.f;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);
    auto* confirm = paimon::ui::makeButton(action, [this] {
        if (!m_options.any()) {
            PaimonNotify::show(tx("Elige al menos una parte.", "Choose at least one part."), NotificationIcon::Warning);
            return;
        }
        auto callback = m_callback;
        auto options = m_options;
        onClose(nullptr);
        if (callback) callback(options);
    }, paimon::ui::Btn::Green, 130.f, 0.5f, "bigFont.fnt");
    confirm->setPosition({W / 2.f, 26.f});
    menu->addChild(confirm);
    return true;
}

} // namespace paimon::iconcopy
