#include "CommunityBasePopup.hpp"
#include "CommunityModerationPopup.hpp"
#include "CommunityMinePopup.hpp"
#include "CommunityShopLayer.hpp"
#include "CommunitySuggestPopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/SettingsBackup.hpp"
#include "../../../core/ModAuthFlow.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <ctime>
#include <memory>

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr float W = 460.f;
constexpr float H = 300.f;

std::string formatDate(int64_t millis) {
    std::time_t seconds = static_cast<std::time_t>(millis / 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}

// suggesting writes as the gd account: logged in and proven first.
bool readyToSuggest() {
    if (!currentIdentity()) {
        PaimonNotify::show(tx("Inicia sesion en Geometry Dash.", "Log in to Geometry Dash."), NotificationIcon::Warning);
        return false;
    }
    if (!hasAccountProof()) {
        promptVerification();
        return false;
    }
    return true;
}
} // namespace

CommunityBasePopup* CommunityBasePopup::create() {
    auto popup = std::make_unique<CommunityBasePopup>();
    if (!popup->init()) return nullptr;
    popup->autorelease();
    return popup.release();
}

void CommunityBasePopup::open() {
    if (auto* popup = create()) showOnTop(popup);
}

bool CommunityBasePopup::init() {
    if (!PaimonPopup::init(W, H)) return false;
    setTitle("Paimon Base Community", "goldFont.fnt", 0.65f);
    addCorners();
    addInfoButton("Paimon Base", tx(
        "Aqui <cg>guardas</c> y <cg>sugieres</c>. Lo publico se ve en <cy>Paimon Shop</c>.\n\n"
        "<cg>Ajustes en la nube</c>: guarda todos tus ajustes y cargalos en otro PC. "
        "No se suben credenciales, URLs de servidores ni rutas de tu equipo. Al cargar se guarda un respaldo local "
        "y el juego se reinicia para aplicarlos.\n\n"
        "<cg>Sugerir</c>: presets, gradientes, iconos de More Icons y fondos de comentario. Un moderador revisa cada envio "
        "antes de que salga en Paimon Shop.\n\n"
        "Para escribir hace falta <cy>verificar tu cuenta de GD</c> una vez (dura 30 dias).",
        "This is where you <cg>save</c> and <cg>suggest</c>. Public items show up in <cy>Paimon Shop</c>.\n\n"
        "<cg>Cloud settings</c>: save every setting and load it on another PC. "
        "Credentials, server URLs and paths on your PC are never uploaded. Loading keeps a local backup "
        "and restarts the game to apply them.\n\n"
        "<cg>Suggest</c>: presets, gradients, More Icons icons and comment backgrounds. A moderator reviews each one "
        "before it shows up in Paimon Shop.\n\n"
        "Writing needs your <cy>GD account verified</c> once (it lasts 30 days)."));
    paimon::markDynamicPopup(this);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    // account
    auto* accountPanel = paimon::ui::makeInset({W - 32.f, 26.f}, 90);
    accountPanel->setPosition({16.f, H - 64.f});
    m_mainLayer->addChild(accountPanel);
    m_account = paimon::ui::makeLabel("", W - 160.f, 0.34f, paimon::ui::palette::text);
    m_account->setAnchorPoint({0.f, 0.5f});
    m_account->setPosition({28.f, H - 51.f});
    m_mainLayer->addChild(m_account);
    m_verify = paimon::ui::makeButton(tx("Verificar", "Verify"), [] { promptVerification(); },
        paimon::ui::Btn::Cyan, 92.f, 0.38f, "bigFont.fnt");
    m_verify->setPosition({W - 70.f, H - 51.f});
    menu->addChild(m_verify);

    // cloud settings
    auto* cloudPanel = paimon::ui::makePanel({W - 32.f, 80.f}, tx("Ajustes en la nube", "Cloud settings"));
    cloudPanel->setPosition({16.f, H - 152.f});
    m_mainLayer->addChild(cloudPanel);
    m_cloud = paimon::ui::makeLabel("", W - 60.f, 0.32f, paimon::ui::palette::muted);
    m_cloud->setPosition({W / 2.f, H - 106.f});
    m_mainLayer->addChild(m_cloud);
    struct Action { char const* label; paimon::ui::Btn skin; void (CommunityBasePopup::*method)(); };
    Action const actions[] = {
        {tx("Guardar", "Save"), paimon::ui::Btn::Green, &CommunityBasePopup::saveCloud},
        {tx("Cargar", "Load"), paimon::ui::Btn::Cyan, &CommunityBasePopup::loadCloud},
        {tx("Borrar", "Delete"), paimon::ui::Btn::Red, &CommunityBasePopup::deleteCloud},
        {tx("Respaldo local", "Local backup"), paimon::ui::Btn::Gray, &CommunityBasePopup::restoreLocal},
    };
    float const actionW = (W - 60.f) / 4.f;
    for (size_t i = 0; i < std::size(actions); ++i) {
        auto method = actions[i].method;
        auto* button = paimon::ui::makeButton(actions[i].label, [this, method] { (this->*method)(); },
            actions[i].skin, actionW - 8.f, 0.38f, "bigFont.fnt");
        button->setPosition({30.f + actionW / 2.f + i * actionW, H - 134.f});
        menu->addChild(button);
        if (i == 3) m_restoreLocal = button;
    }

    // suggestions: everything goes to the review queue, then to paimon shop.
    auto* suggestPanel = paimon::ui::makePanel({W - 32.f, 92.f}, tx("Sugerir para Paimon Shop", "Suggest for Paimon Shop"));
    suggestPanel->setPosition({16.f, 50.f});
    m_mainLayer->addChild(suggestPanel);
    float const cardW = (W - 48.f - 3 * 8.f) / 4.f;
    float const cardH = 58.f;
    auto addSuggestion = [&](int index, char const* title, char const* desc, std::function<void()> action) {
        auto* face = CCNode::create();
        face->setContentSize({cardW, cardH});
        face->addChild(paimon::ui::makeInset({cardW, cardH}, 110));
        auto* label = paimon::ui::makeLabel(title, cardW - 10.f, 0.38f, paimon::ui::palette::gold);
        label->setPosition({cardW / 2.f, cardH - 14.f});
        face->addChild(label);
        auto* text = paimon::ui::makeText(desc, cardW - 10.f, 0.3f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
        text->setPosition({cardW / 2.f, 20.f});
        if (text->getScaledContentSize().height > 30.f) text->setScale(text->getScale() * 30.f / text->getScaledContentSize().height);
        face->addChild(text);
        auto* button = CCMenuItemExt::createSpriteExtra(face, [action](auto*) { action(); });
        button->m_scaleMultiplier = 1.04f;
        button->setPosition({24.f + cardW / 2.f + index * (cardW + 8.f), 50.f + 6.f + cardH / 2.f});
        menu->addChild(button);
    };
    addSuggestion(0, "Preset", tx("De tu biblioteca de Paimon Shop", "From your Paimon Shop library"), [] {
        if (!readyToSuggest()) return;
        if (auto* popup = CommunityPresetPickPopup::create()) showOnTop(popup);
    });
    addSuggestion(1, tx("Gradiente", "Gradient"), tx("Tu gradiente de Icon Gradients", "Your Icon Gradients gradient"), [] {
        if (!readyToSuggest()) return;
        if (auto* popup = CommunitySuggestPopup::create("gradients")) showOnTop(popup);
    });
    addSuggestion(2, tx("Icono", "Icon"), tx("Tu icono de More Icons", "Your More Icons icon"), [] {
        if (!readyToSuggest()) return;
        if (auto* popup = CommunitySuggestPopup::create("icons")) showOnTop(popup);
    });
    addSuggestion(3, tx("Fondo", "Background"), tx("Imagen para comentarios", "Image for comments"), [] {
        if (!readyToSuggest()) return;
        suggestBackground();
    });

    auto* mine = paimon::ui::makeButton(tx("Mis envios", "My submissions"), [] {
        if (auto* popup = CommunityMinePopup::create()) showOnTop(popup);
    }, paimon::ui::Btn::Cyan, 120.f, 0.42f, "bigFont.fnt");
    mine->setPosition({W / 2.f - 140.f, 24.f});
    menu->addChild(mine);
    WeakRef<CommunityBasePopup> weak = this;
    auto* shopButton = paimon::ui::makeButton(tx("Ver en Paimon Shop", "View in Paimon Shop"), [weak] {
        if (auto self = weak.lock()) self->onClose(nullptr);
        CommunityShopLayer::open();
    }, paimon::ui::Btn::Pink, 150.f, 0.42f, "bigFont.fnt");
    shopButton->setPosition({W / 2.f, 24.f});
    menu->addChild(shopButton);
    m_moderation = paimon::ui::makeButton(tx("Moderacion", "Moderation"), [] {
        if (!paimon::modauth::isVerified()) {
            PaimonNotify::show(tx("Solo para moderadores verificados.", "Verified moderators only."), NotificationIcon::Warning);
            return;
        }
        if (auto* popup = CommunityModerationPopup::create()) showOnTop(popup);
    }, paimon::ui::Btn::Gray, 120.f, 0.42f, "bigFont.fnt");
    m_moderation->setPosition({W / 2.f + 140.f, 24.f});
    menu->addChild(m_moderation);
    if (!paimon::modauth::isVerified()) m_moderation->setOpacity(110);

    m_hadProof = hasAccountProof();
    refreshAccount();
    refreshCloud();
    scheduleUpdate();
    return true;
}

// the verification popup stores the token while this popup stays open.
void CommunityBasePopup::update(float) {
    bool proof = hasAccountProof();
    if (proof == m_hadProof) return;
    m_hadProof = proof;
    refreshAccount();
    refreshCloud();
}

void CommunityBasePopup::refreshAccount() {
    auto identity = currentIdentity();
    bool const proof = hasAccountProof();
    std::string text = !identity
        ? std::string(tx("Sin sesion de Geometry Dash", "Not logged in to Geometry Dash"))
        : fmt::format("{}  #{}  -  {}", identity->username, identity->accountID,
            proof ? tx("cuenta verificada", "verified account") : tx("sin verificar", "not verified"));
    m_account->setString(text.c_str());
    m_account->setColor(proof ? paimon::ui::palette::success : paimon::ui::palette::warning);
    m_verify->setVisible(identity && !proof);
    if (m_moderation) m_moderation->setOpacity(paimon::modauth::isVerified() ? 255 : 110);
    enable(m_restoreLocal, settings_backup::hasLocalBackup());
}

void CommunityBasePopup::refreshCloud() {
    if (!currentIdentity()) { m_cloud->setString(tx("Inicia sesion para usar la nube.", "Log in to use the cloud.")); return; }
    if (!hasAccountProof()) { m_cloud->setString(tx("Verifica tu cuenta para ver tu copia.", "Verify your account to see your copy.")); return; }
    if (settings_backup::hasPendingRestore()) {
        m_cloud->setString(tx("Hay una restauracion lista: reinicia el juego para aplicarla.", "A restore is ready: restart the game to apply it."));
        return;
    }
    int token = ++m_token;
    m_cloud->setString(tx("Consultando la nube...", "Checking the cloud..."));
    WeakRef<CommunityBasePopup> weak = this;
    CommunityClient::get().settingsInfo([weak, token](bool ok, matjson::Value const& json, std::string const& error) {
        auto self = weak.lock();
        if (!self || token != self->m_token) return;
        if (!ok) {
            self->m_cloud->setString(error == tx("No se encontro.", "Not found.")
                ? tx("Aun no tienes copia en la nube.", "You have no cloud copy yet.") : error.c_str());
            return;
        }
        auto text = fmt::format("{} {}  |  {} KB  |  {}", tx("Ultima copia:", "Last copy:"),
            formatDate(json["updatedAt"].asInt().unwrapOr(0)), json["bytes"].asInt().unwrapOr(0) / 1024,
            json["modVersion"].asString().unwrapOr("?"));
        self->m_cloud->setString(text.c_str());
    });
}

void CommunityBasePopup::saveCloud() {
    if (m_busy) return;
    auto payload = settings_backup::capture();
    if (!payload) { PaimonNotify::show(payload.unwrapErr().c_str(), NotificationIcon::Error); return; }
    auto summary = settings_backup::summarize(payload.unwrap());
    m_busy = true;
    PaimonNotify::show(tx("Guardando ajustes en la nube...", "Saving settings to the cloud..."), NotificationIcon::Loading);
    WeakRef<CommunityBasePopup> weak = this;
    CommunityClient::get().saveSettings(payload.unwrap(), settings_backup::kSchema,
        [weak, summary](bool ok, matjson::Value const&, std::string const& error) {
            if (auto self = weak.lock()) {
                self->m_busy = false;
                self->refreshCloud();
            }
            if (!ok) { PaimonNotify::show(error.c_str(), NotificationIcon::Error); return; }
            PaimonNotify::show(fmt::format("{} ({} {}, {} {}, {} {})", tx("Ajustes guardados", "Settings saved"),
                summary.settings, tx("ajustes", "settings"), summary.saved, tx("valores", "values"),
                summary.files, tx("archivos", "files")), NotificationIcon::Success);
        });
}

void CommunityBasePopup::loadCloud() {
    if (m_busy) return;
    WeakRef<CommunityBasePopup> weak = this;
    auto* confirm = createQuickPopup("Paimon Base", tx(
        "Tus ajustes se reemplazaran por la copia de la nube. Antes se guarda un <cy>respaldo local</c>. "
        "El juego tiene que <cy>reiniciarse</c> para aplicarlos.",
        "Your settings will be replaced by the cloud copy. A <cy>local backup</c> is kept first. "
        "The game has to <cy>restart</c> to apply them."),
        tx("Cancelar", "Cancel"), tx("Cargar", "Load"), [weak](auto*, bool confirmed) {
            auto self = weak.lock();
            if (!confirmed || !self) return;
            self->m_busy = true;
            CommunityClient::get().loadSettings([weak](bool ok, matjson::Value const& json, std::string const& error) {
                auto self = weak.lock();
                if (self) self->m_busy = false;
                if (!ok) { PaimonNotify::show(error.c_str(), NotificationIcon::Error); return; }
                auto staged = settings_backup::stageRestore(json["payload"]);
                if (!staged) { PaimonNotify::show(staged.unwrapErr().c_str(), NotificationIcon::Error); return; }
                if (self) {
                    self->refreshAccount();
                    self->refreshCloud();
                    self->offerRestart();
                }
            });
        }, false);
    showOnTop(confirm);
}

void CommunityBasePopup::deleteCloud() {
    if (m_busy) return;
    WeakRef<CommunityBasePopup> weak = this;
    auto* confirm = createQuickPopup("Paimon Base", tx("Borrar tu copia de ajustes de la nube?", "Delete your cloud settings copy?"),
        tx("Cancelar", "Cancel"), tx("Borrar", "Delete"), [weak](auto*, bool confirmed) {
            if (!confirmed) return;
            CommunityClient::get().deleteSettings([weak](bool ok, matjson::Value const& json, std::string const& error) {
                if (!ok) { PaimonNotify::show(error.c_str(), NotificationIcon::Error); return; }
                PaimonNotify::show(json["deleted"].asBool().unwrapOr(false)
                    ? tx("Copia borrada.", "Copy deleted.") : tx("No habia copia.", "There was no copy."), NotificationIcon::Success);
                if (auto self = weak.lock()) self->refreshCloud();
            });
        }, false);
    showOnTop(confirm);
}

void CommunityBasePopup::restoreLocal() {
    if (!settings_backup::hasLocalBackup()) return;
    WeakRef<CommunityBasePopup> weak = this;
    auto* confirm = createQuickPopup("Paimon Base", tx(
        "Volver a los ajustes que tenias antes de la ultima carga desde la nube? El juego se reiniciara.",
        "Go back to the settings you had before the last cloud load? The game will restart."),
        tx("Cancelar", "Cancel"), tx("Restaurar", "Restore"), [weak](auto*, bool confirmed) {
            if (!confirmed) return;
            auto staged = settings_backup::stageLocalBackup();
            if (!staged) { PaimonNotify::show(staged.unwrapErr().c_str(), NotificationIcon::Error); return; }
            if (auto self = weak.lock()) {
                self->refreshCloud();
                self->offerRestart();
            }
        }, false);
    showOnTop(confirm);
}

void CommunityBasePopup::offerRestart() {
    auto* confirm = createQuickPopup("Paimon Base", tx(
        "Los ajustes estan listos. Se aplican al <cy>reiniciar</c> el juego.",
        "The settings are ready. They apply when the game <cy>restarts</c>."),
        tx("Mas tarde", "Later"), tx("Reiniciar", "Restart"), [](auto*, bool restart) {
            if (restart) geode::utils::game::restart(true);
        }, false);
    showOnTop(confirm);
}

} // namespace paimon::community
