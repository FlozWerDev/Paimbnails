#include "CommunitySuggestPopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/CommunityItems.hpp"
#include "../../icon-gradients/GradientUtils.hpp"
#include "../../paimon-shop/ui/PaimonShopLayer.hpp"
#include "../../paimon-shop/ui/ShopUI.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <algorithm>
#include <memory>

#include "../../../framework/compat/MoreIconsAPI.hpp"

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr float kSlot = 42.f;
constexpr float kFace = 38.f;
// shader slots for the gamemode dolls; cards and popups use lower ones.
constexpr int kDollShaderBase = 5200;
constexpr size_t kPresetRows = 5;

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}

std::string trimmed(std::string text) {
    auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

// the player's own icon for `type`, painted the way it would be shared.
SimplePlayer* ownDoll(IconType type, bool gradient, int shaderTag) {
    auto* doll = SimplePlayer::create(1);
    if (!doll) return nullptr;
    doll->updatePlayerFrame(paimon::icon_gradients::GradientUtils::getIconID(type, false), type);
    if (auto* gm = GameManager::get()) {
        doll->setColor(gm->colorForIdx(gm->getPlayerColor()));
        doll->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        doll->m_hasGlowOutline = gm->getPlayerGlow();
        if (doll->m_hasGlowOutline) doll->enableCustomGlowColor(gm->colorForIdx(gm->getPlayerGlowColor()));
        doll->updateColors();
    }
    if (gradient) {
        if (items::hasGradient(type)) paimon::icon_gradients::GradientUtils::paintMenuIcon(doll, false, shaderTag);
    } else if (items::hasCustomIcon(type)) {
        more_icons::updateSimplePlayer(doll, more_icons::activeIcon(type));
    }
    doll->setScale(26.f / 32.f);
    return doll;
}
} // namespace

CommunitySuggestPopup* CommunitySuggestPopup::create(std::string const& type) {
    auto popup = std::make_unique<CommunitySuggestPopup>();
    if (!popup->init(type)) return nullptr;
    popup->autorelease();
    return popup.release();
}

CommunitySuggestPopup::~CommunitySuggestPopup() {
    paimon::ui::detachGeodeTextInput(m_title);
    paimon::ui::detachGeodeTextInput(m_description);
}

bool CommunitySuggestPopup::init(std::string const& type) {
    constexpr float W = 420.f, H = 270.f;
    if (!PaimonPopup::init(W, H)) return false;
    m_type = type == "icons" ? "icons" : "gradients";
    bool const gradients = m_type == "gradients";
    setTitle(gradients ? tx("Sugerir gradiente", "Suggest a gradient") : tx("Sugerir icono", "Suggest an icon"), "goldFont.fnt", 0.6f);
    addInfoButton("Paimon Base", gradients
        ? tx("Se envia el gradiente que tienes guardado para ese modo (jugador 1), con sus imagenes. "
             "Un <cy>moderador</c> lo revisa y despues aparece en <cg>Paimon Shop</c> para todos.",
             "Sends the gradient you saved for that gamemode (player 1), with its images. "
             "A <cy>moderator</c> reviews it and then it shows up in <cg>Paimon Shop</c> for everyone.")
        : tx("Se envia el icono de <cy>More Icons</c> que llevas puesto en ese modo (textura y hoja). "
             "Un <cy>moderador</c> lo revisa y despues aparece en <cg>Paimon Shop</c> para todos.",
             "Sends the <cy>More Icons</c> icon you wear in that gamemode (texture and sheet). "
             "A <cy>moderator</c> reviews it and then it shows up in <cg>Paimon Shop</c> for everyone."));
    paimon::markDynamicPopup(this);

    auto* hint = paimon::ui::makeLabel(gradients
        ? tx("Elige el modo del gradiente que quieres compartir", "Pick the gamemode of the gradient to share")
        : tx("Elige el modo del icono custom que llevas puesto", "Pick the gamemode of the custom icon you wear"),
        W - 40.f, 0.3f, paimon::ui::palette::muted);
    hint->setPosition({W / 2.f, H - 46.f});
    m_mainLayer->addChild(hint);

    m_highlight = paimon::ui::makeInset({kFace + 4.f, kFace + 4.f}, 200, paimon::ui::palette::gold);
    m_highlight->setVisible(false);
    m_mainLayer->addChild(m_highlight, 1);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 3);
    auto const& modes = items::gamemodes();
    float const left = (W - modes.size() * kSlot) / 2.f;
    int firstAvailable = -1;
    for (size_t i = 0; i < modes.size(); ++i) {
        auto mode = modes[i];
        bool const available = gradients ? items::hasGradient(mode) : items::hasCustomIcon(mode);
        auto* face = CCNode::create();
        face->setContentSize({kFace, kFace});
        face->addChild(paimon::ui::makeInset({kFace, kFace}, 120));
        if (auto* doll = ownDoll(mode, gradients, kDollShaderBase + static_cast<int>(i))) {
            doll->setPosition({kFace / 2.f, kFace / 2.f});
            if (!available) doll->setOpacity(70);
            face->addChild(doll);
        }
        int const index = static_cast<int>(i);
        auto* button = CCMenuItemExt::createSpriteExtra(face, [this, index](auto*) { select(index); });
        button->setPosition({left + kSlot / 2.f + i * kSlot, H - 82.f});
        button->setEnabled(available);
        menu->addChild(button);
        m_modes.push_back(button);
        if (available && firstAvailable < 0) firstAvailable = index;
    }

    m_mode = paimon::ui::makeLabel("", W - 40.f, 0.34f, paimon::ui::palette::gold);
    m_mode->setPosition({W / 2.f, H - 114.f});
    m_mainLayer->addChild(m_mode);

    m_title = TextInput::create(300.f, tx("Nombre", "Name"), "chatFont.fnt");
    m_title->setCommonFilter(CommonFilter::Text);
    m_title->setMaxCharCount(48);
    m_title->setPosition({W / 2.f, 120.f});
    m_mainLayer->addChild(m_title, 4);
    m_description = TextInput::create(360.f, tx("Descripcion (opcional)", "Description (optional)"), "chatFont.fnt");
    m_description->setCommonFilter(CommonFilter::TextEmojis);
    m_description->setMaxCharCount(200);
    m_description->setPosition({W / 2.f, 82.f});
    m_mainLayer->addChild(m_description, 4);

    m_send = paimon::ui::makeButton(tx("Enviar", "Send"), [this] { send(); }, paimon::ui::Btn::Green, 110.f, 0.5f, "bigFont.fnt");
    m_send->setPosition({W / 2.f, 30.f});
    menu->addChild(m_send);

    if (firstAvailable >= 0) {
        select(firstAvailable);
    } else {
        m_mode->setString(gradients
            ? tx("No tienes gradientes guardados. Crea uno en Icon Gradients.", "You have no saved gradients. Make one in Icon Gradients.")
            : tx("No llevas iconos de More Icons. Ponte uno primero.", "You wear no More Icons icons. Equip one first."));
        m_mode->setColor(paimon::ui::palette::warning);
        enable(m_send, false);
    }
    return true;
}

void CommunitySuggestPopup::select(int index) {
    if (index < 0 || index >= static_cast<int>(m_modes.size())) return;
    m_selected = index;
    m_highlight->setPosition(m_modes[index]->getPosition() - CCPoint{kFace / 2.f + 2.f, kFace / 2.f + 2.f});
    m_highlight->setVisible(true);
    auto mode = items::gamemodes()[index];
    std::string text = items::gamemodeName(mode);
    if (m_type == "icons") {
        if (auto* info = more_icons::activeIcon(mode)) {
            auto name = info->getShortName().empty() ? info->getName() : info->getShortName();
            text += "  |  " + name;
            if (std::string(m_title->getString()).empty()) m_title->setString(name.substr(0, 48));
        }
    }
    m_mode->setString(text.c_str());
    m_mode->setColor(paimon::ui::palette::gold);
}

void CommunitySuggestPopup::send() {
    if (m_busy || m_selected < 0) return;
    if (!currentIdentity()) {
        PaimonNotify::show(tx("Inicia sesion en Geometry Dash.", "Log in to Geometry Dash."), NotificationIcon::Warning);
        return;
    }
    if (!hasAccountProof()) {
        promptVerification();
        return;
    }
    auto title = trimmed(std::string(m_title->getString()));
    if (title.empty()) {
        PaimonNotify::show(tx("Ponle un nombre.", "Give it a name."), NotificationIcon::Warning);
        return;
    }
    auto description = trimmed(std::string(m_description->getString()));
    m_busy = true;
    enable(m_send, false);
    PaimonNotify::show(tx("Enviando a Paimon Base...", "Sending to Paimon Base..."), NotificationIcon::Loading);
    WeakRef<CommunitySuggestPopup> weak = this;
    auto done = [weak](bool ok, std::string const& message) {
        PaimonNotify::show(message, ok ? NotificationIcon::Success : NotificationIcon::Error);
        auto self = weak.lock();
        if (!self) return;
        self->m_busy = false;
        enable(self->m_send, true);
        if (ok) self->onClose(nullptr);
    };
    auto mode = items::gamemodes()[m_selected];
    if (m_type == "gradients") items::submitGradient(mode, title, description, done);
    else items::submitIcon(mode, title, description, done);
}

CommunityPresetPickPopup* CommunityPresetPickPopup::create() {
    auto popup = std::make_unique<CommunityPresetPickPopup>();
    if (!popup->init()) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool CommunityPresetPickPopup::init() {
    constexpr float W = 400.f, H = 260.f;
    if (!PaimonPopup::init(W, H)) return false;
    setTitle(tx("Sugerir preset", "Suggest a preset"), "goldFont.fnt", 0.6f);
    addInfoButton("Paimon Base", tx(
        "Elige un preset de tu biblioteca de <cg>Paimon Shop</c>. Se envia con su preview y lo revisa un <cy>moderador</c>.\n\n"
        "Para crear uno, abre Paimon Shop y pulsa <cg>+ Crear</c>: guarda tus ajustes actuales de esa categoria.",
        "Pick a preset from your <cg>Paimon Shop</c> library. It is sent with its preview and a <cy>moderator</c> reviews it.\n\n"
        "To make one, open Paimon Shop and press <cg>+ Create</c>: it saves your current settings for that category."));
    paimon::markDynamicPopup(this);
    m_list = CCNode::create();
    m_mainLayer->addChild(m_list, 2);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 3);
    m_prev = paimon::ui::makeButton("<", [this] { if (m_page > 0) { --m_page; refresh(); } }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    m_prev->setPosition({30.f, 24.f});
    menu->addChild(m_prev);
    m_next = paimon::ui::makeButton(">", [this] { ++m_page; refresh(); }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    m_next->setPosition({W - 30.f, 24.f});
    menu->addChild(m_next);
    refresh();
    return true;
}

void CommunityPresetPickPopup::refresh() {
    m_list->removeAllChildren();
    float const W = m_mainLayer->getContentSize().width;
    enable(m_prev, false);
    enable(m_next, false);
    auto entries = shop::localLibrary().list();
    if (!entries || entries.unwrap().empty()) {
        auto* empty = paimon::ui::makeText(entries
            ? tx("Tu biblioteca de Paimon Shop esta vacia.\nCrea un preset con tus ajustes primero.",
                 "Your Paimon Shop library is empty.\nMake a preset from your settings first.")
            : entries.unwrapErr().c_str(), W - 60.f, 0.45f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
        empty->setPosition({W / 2.f, 140.f});
        m_list->addChild(empty);
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_list->addChild(menu);
        WeakRef<CommunityPresetPickPopup> weak = this;
        auto* openShop = paimon::ui::makeButton(tx("Abrir Paimon Shop", "Open Paimon Shop"), [weak] {
            if (auto self = weak.lock()) self->onClose(nullptr);
            shop::PaimonShopLayer::open();
        }, paimon::ui::Btn::Pink, 150.f, 0.45f, "bigFont.fnt");
        openShop->setPosition({W / 2.f, 82.f});
        menu->addChild(openShop);
        return;
    }
    auto listings = std::move(entries).unwrap();
    std::stable_sort(listings.begin(), listings.end(), [](auto const& a, auto const& b) { return a.createdAt > b.createdAt; });
    size_t const pages = (listings.size() + kPresetRows - 1) / kPresetRows;
    m_page = std::min(m_page, pages - 1);
    enable(m_prev, m_page > 0);
    enable(m_next, m_page + 1 < pages);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_list->addChild(menu);
    float y = 196.f;
    for (size_t i = m_page * kPresetRows; i < std::min(listings.size(), (m_page + 1) * kPresetRows); ++i) {
        auto const& entry = listings[i];
        auto* row = paimon::ui::makeInset({W - 40.f, 28.f}, 90);
        row->setPosition({20.f, y - 14.f});
        m_list->addChild(row);
        auto* title = paimon::ui::makeLabel(entry.title.c_str(), 190.f, 0.34f, paimon::ui::palette::text);
        title->setAnchorPoint({0.f, 0.5f});
        title->setPosition({30.f, y});
        m_list->addChild(title);
        auto* category = paimon::ui::makeLabel(shop::categoryName(entry.category), 90.f, 0.28f, paimon::ui::palette::info);
        category->setAnchorPoint({0.f, 0.5f});
        category->setPosition({228.f, y});
        m_list->addChild(category);
        auto id = entry.id;
        auto* send = paimon::ui::makeButton(tx("Enviar", "Send"), [this, id] { publish(id); },
            paimon::ui::Btn::Pink, 64.f, 0.34f, "bigFont.fnt");
        send->setPosition({W - 58.f, y});
        menu->addChild(send);
        y -= 34.f;
    }
}

void CommunityPresetPickPopup::publish(std::string const& id) {
    auto preset = shop::localLibrary().read(id);
    if (!preset) { PaimonNotify::show(preset.unwrapErr(), NotificationIcon::Error); return; }
    auto value = std::move(preset).unwrap();
    auto message = fmt::format("{} <cy>{}</c> {}", tx("Enviar", "Send"), value.title,
        tx("a Paimon Base? Un moderador lo revisa antes de publicarlo en Paimon Shop.",
           "to Paimon Base? A moderator reviews it before it shows up in Paimon Shop."));
    auto* confirm = createQuickPopup("Paimon Base", message, tx("Cancelar", "Cancel"), tx("Enviar", "Send"),
        [value](auto*, bool confirmed) {
            if (!confirmed) return;
            PaimonNotify::show(tx("Enviando preset...", "Sending preset..."), NotificationIcon::Loading);
            items::publishPreset(value, [](bool ok, std::string const& text) {
                PaimonNotify::show(text, ok ? NotificationIcon::Success : NotificationIcon::Error);
            });
        }, false);
    showOnTop(confirm);
}

} // namespace paimon::community
