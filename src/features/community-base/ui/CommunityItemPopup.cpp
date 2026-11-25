#include "CommunityItemPopup.hpp"
#include "CommunityBackgroundsPopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/CommunityItems.hpp"
#include "../services/CommunityMediaCache.hpp"
#include "../../global-icon/GlobalIconTypes.hpp"
#include "../../paimon-shop/services/ShopConfiguration.hpp"
#include "../../paimon-shop/ui/ShopUI.hpp"
#include "../../../core/ModAuthFlow.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <memory>
#include <vector>

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr float W = 440.f;
constexpr float H = 280.f;
constexpr int kPreviewShaderSlot = 20;

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}

// category for presets, gamemode for gradients and icons.
std::string itemTag(matjson::Value const& item) {
    auto type = item["type"].asString().unwrapOr("");
    if (type == "presets") {
        auto category = shop::parseCategory(item["category"].asString().unwrapOr(""));
        return category ? shop::categoryName(category.unwrap()) : "Preset";
    }
    if (type == "backgrounds") return tx("Fondo de comentarios", "Comment background");
    auto iconType = paimon::globalicon::iconTypeFromString(item["iconType"].asString().unwrapOr(""));
    auto mode = iconType ? items::gamemodeName(*iconType) : "?";
    return fmt::format("{}  |  {}", type == "gradients" ? tx("Gradiente", "Gradient") : tx("Icono", "Icon"), mode);
}
} // namespace

CommunityItemPopup* CommunityItemPopup::create(matjson::Value item, std::function<void()> onChanged) {
    auto popup = std::make_unique<CommunityItemPopup>();
    if (!popup->init(std::move(item), std::move(onChanged))) return nullptr;
    popup->autorelease();
    return popup.release();
}

void CommunityItemPopup::onClose(CCObject* sender) {
    auto callback = m_changed ? m_onChanged : nullptr;
    m_onChanged = nullptr;
    PaimonPopup::onClose(sender);
    if (callback) callback();
}

bool CommunityItemPopup::init(matjson::Value item, std::function<void()> onChanged) {
    if (!PaimonPopup::init(W, H)) return false;
    m_item = std::move(item);
    m_onChanged = std::move(onChanged);
    // const access: matjson's non-const operator[] would insert missing keys.
    matjson::Value const& data = m_item;
    m_type = data["type"].asString().unwrapOr("");
    m_id = data["id"].asString().unwrapOr("");
    m_likes = static_cast<int>(data["likes"].asInt().unwrapOr(0));
    m_dislikes = static_cast<int>(data["dislikes"].asInt().unwrapOr(0));
    m_downloads = static_cast<int>(data["downloads"].asInt().unwrapOr(0));
    m_myVote = static_cast<int>(data["myVote"].asInt().unwrapOr(0));
    auto identity = currentIdentity();
    int const owner = static_cast<int>(data["accountID"].asInt().unwrapOr(0));
    m_own = identity && identity->accountID == owner;
    setTitle(data["title"].asString().unwrapOr(""), "goldFont.fnt", 0.6f);
    paimon::markDynamicPopup(this);

    auto* preview = makeItemPreview(data, {224.f, 132.f}, kPreviewShaderSlot);
    preview->setPosition({16.f, 99.f});
    m_mainLayer->addChild(preview);

    auto addLabel = [this](std::string const& value, float y, float scale, ccColor3B color) {
        auto* label = paimon::ui::makeLabel(value.c_str(), 172.f, scale, color);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({252.f, y});
        m_mainLayer->addChild(label);
    };
    addLabel(itemTag(data), 224.f, 0.34f, paimon::ui::palette::info);
    addLabel(fmt::format("{} {}", tx("por", "by"), data["username"].asString().unwrapOr("")), 206.f, 0.34f, paimon::ui::palette::gold);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 4);

    // likes and dislikes are separate buttons; pressing the active one removes the vote.
    m_like = paimon::ui::makeButton(tx("Me gusta", "Like"), [this] { vote(m_myVote == 1 ? 0 : 1); },
        paimon::ui::Btn::Gray, 84.f, 0.4f, "bigFont.fnt");
    m_like->setPosition({296.f, 146.f});
    menu->addChild(m_like);
    m_dislike = paimon::ui::makeButton(tx("No me gusta", "Dislike"), [this] { vote(m_myVote == -1 ? 0 : -1); },
        paimon::ui::Btn::Gray, 84.f, 0.4f, "bigFont.fnt");
    m_dislike->setPosition({384.f, 146.f});
    menu->addChild(m_dislike);
    if (m_own) {
        addLabel(tx("Es tu envio: no puedes calificarlo.", "Your submission: you cannot rate it."), 124.f, 0.26f, paimon::ui::palette::dim);
    }

    auto descriptionText = data["description"].asString().unwrapOr("");
    auto* description = paimon::ui::makeText(descriptionText.empty() ? tx("Sin descripcion", "No description") : descriptionText.c_str(), 400.f, 0.43f);
    description->setAnchorPoint({0.f, 1.f});
    description->setPosition({20.f, 90.f});
    if (description->getScaledContentSize().height > 40.f) {
        description->setScale(description->getScale() * 40.f / description->getScaledContentSize().height);
    }
    m_mainLayer->addChild(description);

    // per-type actions along the bottom.
    struct Action { char const* label; paimon::ui::Btn skin; float width; std::function<void()> run; };
    std::vector<Action> actions;
    if (m_type == "presets") {
        actions.push_back({tx("Guardar en Shop", "Save to Shop"), paimon::ui::Btn::Cyan, 124.f, [this] { savePreset(false); }});
        actions.push_back({tx("Aplicar", "Apply"), paimon::ui::Btn::Green, 90.f, [this] { savePreset(true); }});
    } else if (m_type == "gradients") {
        actions.push_back({tx("Guardar", "Save"), paimon::ui::Btn::Cyan, 96.f, [this] { takeGradient(false); }});
        actions.push_back({tx("Aplicar", "Apply"), paimon::ui::Btn::Green, 90.f, [this] { takeGradient(true); }});
    } else if (m_type == "icons") {
        actions.push_back({tx("Descargar", "Download"), paimon::ui::Btn::Cyan, 106.f, [this] { takeIcon(false); }});
        actions.push_back({tx("Descargar y usar", "Download and use"), paimon::ui::Btn::Green, 136.f, [this] { takeIcon(true); }});
    } else if (m_type == "backgrounds") {
        actions.push_back({tx("Usar en comentarios", "Use for comments"), paimon::ui::Btn::Green, 160.f, [this] { useBackground(); }});
    }
    if (identity && (m_own || paimon::modauth::isVerified())) {
        actions.push_back({tx("Borrar", "Delete"), paimon::ui::Btn::Red, 80.f, [this] { remove(); }});
    }
    float total = 0.f;
    for (auto const& action : actions) total += action.width + 10.f;
    float x = (W - (total - 10.f)) / 2.f;
    for (auto& action : actions) {
        auto* button = paimon::ui::makeButton(action.label, std::move(action.run), action.skin, action.width, 0.42f, "bigFont.fnt");
        button->setPosition({x + action.width / 2.f, 26.f});
        menu->addChild(button);
        x += action.width + 10.f;
    }
    if (m_type == "gradients" || m_type == "icons") {
        auto* hint = paimon::ui::makeLabel(m_type == "gradients"
            ? tx("Guardar: biblioteca de Icon Gradients. Aplicar: reemplaza el de ese modo.",
                 "Save: Icon Gradients library. Apply: replaces the one for that gamemode.")
            : tx("Se copia a tu carpeta de More Icons y queda tras reiniciar.",
                 "It is copied to your More Icons folder and stays after a restart."),
            W - 40.f, 0.24f, paimon::ui::palette::dim);
        hint->setPosition({W / 2.f, 46.f});
        m_mainLayer->addChild(hint);
    }

    refreshRating();
    return true;
}

void CommunityItemPopup::refreshRating() {
    if (m_counters) m_counters->removeFromParent();
    m_counters = makeCounters(m_likes, m_dislikes, m_downloads, 0.42f);
    m_counters->setPosition({252.f, 180.f});
    if (m_counters->getContentWidth() > 172.f) m_counters->setScale(172.f / m_counters->getContentWidth());
    m_mainLayer->addChild(m_counters);
    paimon::ui::setButtonSkin(m_like, m_myVote == 1 ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    paimon::ui::setButtonSkin(m_dislike, m_myVote == -1 ? paimon::ui::Btn::Red : paimon::ui::Btn::Gray);
    enable(m_like, !m_own && !m_voting);
    enable(m_dislike, !m_own && !m_voting);
}

void CommunityItemPopup::readCounts(matjson::Value const& json) {
    if (json.contains("likes")) m_likes = static_cast<int>(json["likes"].asInt().unwrapOr(m_likes));
    if (json.contains("dislikes")) m_dislikes = static_cast<int>(json["dislikes"].asInt().unwrapOr(m_dislikes));
    if (json.contains("downloads")) m_downloads = static_cast<int>(json["downloads"].asInt().unwrapOr(m_downloads));
}

void CommunityItemPopup::vote(int value) {
    if (m_voting || m_own) return;
    if (!currentIdentity()) {
        PaimonNotify::show(tx("Inicia sesion en Geometry Dash para calificar.", "Log in to Geometry Dash to rate."), NotificationIcon::Warning);
        return;
    }
    if (!hasAccountProof()) {
        promptVerification();
        return;
    }
    m_voting = true;
    refreshRating();
    WeakRef<CommunityItemPopup> weak = this;
    CommunityClient::get().vote(m_type, m_id, value, [weak](bool ok, matjson::Value const& json, std::string const& error) {
        auto self = weak.lock();
        if (!self) return;
        self->m_voting = false;
        if (!ok) {
            PaimonNotify::show(error, NotificationIcon::Error);
            self->refreshRating();
            return;
        }
        self->m_myVote = static_cast<int>(json["myVote"].asInt().unwrapOr(0));
        self->readCounts(json);
        self->m_changed = true;
        self->refreshRating();
    });
}

void CommunityItemPopup::counted() {
    WeakRef<CommunityItemPopup> weak = this;
    CommunityClient::get().countDownload(m_type, m_id, [weak](bool ok, matjson::Value const& json, std::string const&) {
        auto self = weak.lock();
        if (!self || !ok) return;
        self->readCounts(json);
        self->m_changed = true;
        self->refreshRating();
    });
}

void CommunityItemPopup::savePreset(bool apply) {
    if (m_busy) return;
    m_busy = true;
    PaimonNotify::show(tx("Descargando preset...", "Downloading preset..."), NotificationIcon::Loading);
    WeakRef<CommunityItemPopup> weak = this;
    items::fetchPreset(m_item, [weak, apply](geode::Result<shop::Preset> result) {
        auto self = weak.lock();
        if (self) self->m_busy = false;
        if (!result) { PaimonNotify::show(result.unwrapErr(), NotificationIcon::Error); return; }
        auto preset = std::move(result).unwrap();
        if (!apply) {
            auto added = shop::localLibrary().add(preset);
            if (!added) { PaimonNotify::show(added.unwrapErr(), NotificationIcon::Error); return; }
            PaimonNotify::show(tx("Guardado en tu biblioteca de Paimon Shop.", "Saved to your Paimon Shop library."), NotificationIcon::Success);
            if (self) self->counted();
            return;
        }
        auto* confirm = createQuickPopup("Paimon Shop", tx(
            "Se reemplazaran los ajustes de esta categoria. Paimon Shop guarda un <cy>respaldo</c> para restaurarlos.",
            "This replaces the settings in this category. Paimon Shop keeps a <cy>backup</c> to restore them."),
            tx("Cancelar", "Cancel"), tx("Aplicar", "Apply"), [weak, preset](auto*, bool confirmed) {
                if (!confirmed) return;
                auto applied = shop::applyConfiguration(preset.category, preset.configuration);
                if (!applied) { PaimonNotify::show(applied.unwrapErr(), NotificationIcon::Error); return; }
                PaimonNotify::show(tx("Preset aplicado. Respaldo guardado.", "Preset applied. Backup saved."), NotificationIcon::Success);
                if (auto self = weak.lock()) self->counted();
            }, false);
        showOnTop(confirm);
    });
}

void CommunityItemPopup::takeGradient(bool apply) {
    if (m_busy) return;
    m_busy = true;
    WeakRef<CommunityItemPopup> weak = this;
    items::takeGradient(m_item, apply, [weak](bool ok, std::string const& message) {
        PaimonNotify::show(message, ok ? NotificationIcon::Success : NotificationIcon::Error);
        auto self = weak.lock();
        if (!self) return;
        self->m_busy = false;
        if (ok) self->counted();
    });
}

void CommunityItemPopup::takeIcon(bool use) {
    if (m_busy) return;
    m_busy = true;
    PaimonNotify::show(tx("Descargando icono...", "Downloading icon..."), NotificationIcon::Loading);
    WeakRef<CommunityItemPopup> weak = this;
    items::takeIcon(m_item, use, [weak](bool ok, std::string const& message) {
        PaimonNotify::show(message, ok ? NotificationIcon::Success : NotificationIcon::Error);
        auto self = weak.lock();
        if (!self) return;
        self->m_busy = false;
        if (ok) self->counted();
    });
}

void CommunityItemPopup::useBackground() {
    if (m_busy) return;
    m_busy = true;
    WeakRef<CommunityItemPopup> weak = this;
    useBackgroundForComments(m_id, [weak](bool ok) {
        auto self = weak.lock();
        if (!self) return;
        self->m_busy = false;
        if (ok) self->counted();
    });
}

void CommunityItemPopup::remove() {
    WeakRef<CommunityItemPopup> weak = this;
    auto* confirm = createQuickPopup("Paimon Base", tx("Borrar esto de Paimon Base? No se puede deshacer.", "Delete this from Paimon Base? It cannot be undone."),
        tx("Cancelar", "Cancel"), tx("Borrar", "Delete"), [weak](auto*, bool confirmed) {
            auto self = weak.lock();
            if (!confirmed || !self || self->m_busy) return;
            self->m_busy = true;
            CommunityClient::get().deleteItem(self->m_type, self->m_id, [weak](bool ok, matjson::Value const&, std::string const& error) {
                auto self = weak.lock();
                if (self) self->m_busy = false;
                if (!ok) { PaimonNotify::show(error, NotificationIcon::Error); return; }
                PaimonNotify::show(tx("Borrado.", "Deleted."), NotificationIcon::Success);
                if (!self) return;
                matjson::Value const& item = self->m_item;
                for (auto key : {"previewUrl", "imageUrl"}) {
                    if (auto url = item[key].asString().unwrapOr(""); !url.empty()) CommunityMediaCache::get().forget(url);
                }
                self->m_changed = true;
                self->onClose(nullptr);
            });
        }, false);
    showOnTop(confirm);
}

} // namespace paimon::community
