#include "GlobalIconLinkPopup.hpp"
#include "../services/GlobalIconService.hpp"
#include "../../community-base/services/CommunityClient.hpp"
#include "../../../framework/compat/ModCompat.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/CCMenuItemToggler.hpp>
#include <memory>

using namespace geode::prelude;

namespace paimon::globalicon {

using community::tx;

GlobalIconLinkPopup* GlobalIconLinkPopup::create(LinkOptions current, bool enabled, Callback callback) {
    auto popup = std::make_unique<GlobalIconLinkPopup>();
    if (!popup->init(current, enabled, std::move(callback))) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool GlobalIconLinkPopup::init(LinkOptions current, bool enabled, Callback callback) {
    constexpr float W = 350.f, H = 250.f;
    if (!PaimonPopup::init(W, H)) return false;
    m_callback = std::move(callback);
    bool const hasGradients = GlobalIconService::hasGradients();
    bool const hasCustom = GlobalIconService::hasCustomIcons();
    m_options = {current.vanilla, current.gradients && hasGradients, current.custom && hasCustom};
    if (!m_options.any()) m_options.vanilla = true;

    setTitle(tx("Que quieres vincular?", "What do you want to link?"), "goldFont.fnt", 0.6f);
    addInfoButton("Global Icon", tx(
        "Lo que vincules se guarda en <cy>Paimon Base</c> y lo ven los demas en tu perfil. "
        "Al copiar tus iconos podran elegir que partes llevarse.\n\n"
        "<cg>Vanilla</c>: ids y colores de GD.\n<cg>Gradientes</c>: Icon Gradients, con sus imagenes.\n"
        "<cg>Custom</c>: las texturas de More Icons que llevas puestas.",
        "What you link is stored in <cy>Paimon Base</c> and others see it on your profile. "
        "When they copy your icons they can choose which parts to take.\n\n"
        "<cg>Vanilla</c>: GD icon ids and colors.\n<cg>Gradients</c>: Icon Gradients with their images.\n"
        "<cg>Custom</c>: the More Icons textures you are wearing."));
    paimon::markDynamicPopup(this);

    float const rowW = W - 40.f;
    struct RowSpec {
        char const* title;
        char const* desc;
        bool available;
        bool* value;
    };
    RowSpec rows[] = {
        {tx("Iconos vanilla", "Vanilla icons"), tx("Tus iconos y colores de GD.", "Your GD icons and colors."), true, &m_options.vanilla},
        {tx("Gradientes", "Gradients"), hasGradients ? tx("Los gradientes de Icon Gradients.", "Your Icon Gradients.")
            : tx("No tienes gradientes guardados.", "You have no saved gradients."), hasGradients, &m_options.gradients},
        {tx("Iconos custom", "Custom icons"), hasCustom ? tx("Las texturas de More Icons que llevas.", "The More Icons textures you wear.")
            : paimon::compat::ModCompat::isMoreIconsLoaded() ? tx("No llevas iconos custom ahora.", "You are not wearing custom icons.")
            : tx("Necesita More Icons.", "Requires More Icons."), hasCustom, &m_options.custom},
    };
    float y = H - 66.f;
    for (auto const& spec : rows) {
        CCMenuItemToggler* toggle = nullptr;
        auto* value = spec.value;
        auto* row = paimon::configkit::makeToggleRow(rowW, spec.title, spec.desc, *value,
            [value](bool on) { *value = on; }, &toggle);
        row->ignoreAnchorPointForPosition(false);
        row->setAnchorPoint({0.5f, 0.5f});
        row->setPosition({W / 2.f, y});
        m_mainLayer->addChild(row);
        if (!spec.available && toggle) {
            toggle->setEnabled(false);
            toggle->setOpacity(110);
        }
        y -= row->getContentSize().height + 6.f;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);
    auto* link = paimon::ui::makeButton(enabled ? tx("Actualizar", "Update") : tx("Vincular", "Link"),
        [this] { finish(true); }, paimon::ui::Btn::Green, 120.f, 0.5f, "bigFont.fnt");
    menu->addChild(link);
    if (enabled) {
        auto* stop = paimon::ui::makeButton(tx("Dejar de compartir", "Stop sharing"), [this] { finish(false); },
            paimon::ui::Btn::Red, 140.f, 0.5f, "bigFont.fnt");
        stop->setPosition({W / 2.f + 75.f, 28.f});
        link->setPosition({W / 2.f - 80.f, 28.f});
        menu->addChild(stop);
    } else {
        link->setPosition({W / 2.f, 28.f});
    }
    return true;
}

void GlobalIconLinkPopup::finish(bool link) {
    if (link && !m_options.any()) {
        PaimonNotify::show(tx("Elige al menos una parte.", "Choose at least one part."), NotificationIcon::Warning);
        return;
    }
    auto callback = m_callback;
    auto options = m_options;
    onClose(nullptr);
    if (callback) callback(link, options);
}

} // namespace paimon::globalicon
