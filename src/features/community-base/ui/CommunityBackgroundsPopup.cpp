#include "CommunityBackgroundsPopup.hpp"
#include "CommunityItemPopup.hpp"
#include "CommunityMinePopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../../../managers/ThumbnailAPI.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../profiles/services/ProfileThumbs.hpp"

#include <memory>

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr float kCellW = 128.f;
constexpr float kCellH = 72.f;
constexpr int kColumns = 3;
constexpr int kPageSize = 6;

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}
} // namespace

CommunityBackgroundsPopup* CommunityBackgroundsPopup::create(PickCallback onPick) {
    auto popup = std::make_unique<CommunityBackgroundsPopup>();
    if (!popup->init(std::move(onPick))) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool CommunityBackgroundsPopup::init(PickCallback onPick) {
    constexpr float W = 440.f, H = 300.f;
    if (!PaimonPopup::init(W, H)) return false;
    m_onPick = std::move(onPick);
    setTitle(tx("Fondos de la comunidad", "Community backgrounds"), "goldFont.fnt", 0.6f);
    addInfoButton("Paimon Base", tx(
        "Fondos para tus comentarios sugeridos por jugadores. Cada uno lo revisa un <cy>moderador</c> antes de salir aqui.\n\n"
        "<cg>Sugerir</c> envia tu imagen (PNG, JPG o WEBP, maximo 4 MB) a la cola de revision.",
        "Comment backgrounds suggested by players. A <cy>moderator</c> reviews each one before it shows up here.\n\n"
        "<cg>Suggest</c> sends your image (PNG, JPG or WEBP, up to 4 MB) to the review queue."));
    paimon::markDynamicPopup(this);

    m_grid = CCMenu::create();
    m_grid->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_grid, 3);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 4);
    m_prev = paimon::ui::makeButton("<", [this] {
        if (m_offset == 0) return;
        m_offset = std::max(0, m_offset - kPageSize);
        load();
    }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    m_prev->setPosition({30.f, 26.f});
    menu->addChild(m_prev);
    m_next = paimon::ui::makeButton(">", [this] {
        if (!m_hasMore) return;
        m_offset += kPageSize;
        load();
    }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    m_next->setPosition({W - 30.f, 26.f});
    menu->addChild(m_next);
    auto* suggest = paimon::ui::makeButton(tx("Sugerir", "Suggest"), [] { suggestBackground(); },
        paimon::ui::Btn::Green, 100.f, 0.45f, "bigFont.fnt");
    suggest->setPosition({W / 2.f - 60.f, 26.f});
    menu->addChild(suggest);
    auto* mine = paimon::ui::makeButton(tx("Mis envios", "My submissions"), [] {
        if (auto* popup = CommunityMinePopup::create("backgrounds")) showOnTop(popup);
    }, paimon::ui::Btn::Cyan, 110.f, 0.45f, "bigFont.fnt");
    mine->setPosition({W / 2.f + 62.f, 26.f});
    menu->addChild(mine);

    m_status = paimon::ui::makeLabel("", W - 60.f, 0.34f, paimon::ui::palette::muted);
    m_status->setPosition({W / 2.f, H / 2.f});
    m_mainLayer->addChild(m_status, 5);
    load();
    return true;
}

void CommunityBackgroundsPopup::load() {
    int token = ++m_token;
    m_grid->removeAllChildren();
    m_status->setString(tx("Cargando...", "Loading..."));
    m_status->setVisible(true);
    enable(m_prev, false);
    enable(m_next, false);
    WeakRef<CommunityBackgroundsPopup> weak = this;
    CommunityClient::get().listItems("backgrounds", "", "new", m_offset, kPageSize,
        [weak, token](bool ok, matjson::Value const& json, std::string const& error) {
            auto self = weak.lock();
            if (!self || token != self->m_token) return;
            if (!ok) { self->m_status->setString(error.c_str()); return; }
            self->m_hasMore = json["hasMore"].asBool().unwrapOr(false);
            enable(self->m_prev, self->m_offset > 0);
            enable(self->m_next, self->m_hasMore);
            auto const& list = json["items"];
            if (!list.isArray() || list.size() == 0) {
                self->m_status->setString(tx("Aun no hay fondos aprobados. Sugiere el primero!", "No approved backgrounds yet. Suggest the first one!"));
                return;
            }
            self->m_status->setVisible(false);
            float const W = self->m_mainLayer->getContentSize().width;
            float const left = (W - (kColumns * kCellW + (kColumns - 1) * 10.f)) / 2.f;
            int index = 0;
            for (auto const& item : list) {
                if (index >= kPageSize) break;
                if (!isCommunityId(item["id"].asString().unwrapOr(""))) continue;
                auto* face = CCNode::create();
                face->setContentSize({kCellW, kCellH + 26.f});
                auto* image = makeRemoteImage(item["imageUrl"].asString().unwrapOr(""), {kCellW, kCellH});
                image->setPosition({0.f, 26.f});
                face->addChild(image);
                auto* label = paimon::ui::makeLabel(item["title"].asString().unwrapOr("").c_str(), kCellW - 4.f, 0.3f, paimon::ui::palette::text);
                label->setPosition({kCellW / 2.f, 18.f});
                face->addChild(label);
                auto* counters = makeCounters(static_cast<int>(item["likes"].asInt().unwrapOr(0)),
                    static_cast<int>(item["dislikes"].asInt().unwrapOr(0)), static_cast<int>(item["downloads"].asInt().unwrapOr(0)), 0.24f);
                if (counters->getContentWidth() > kCellW - 8.f) counters->setScale((kCellW - 8.f) / counters->getContentWidth());
                counters->setPosition({(kCellW - counters->getScaledContentWidth()) / 2.f, 6.f});
                face->addChild(counters);
                auto* button = CCMenuItemExt::createSpriteExtra(face, [weak, item](auto*) {
                    if (auto self = weak.lock()) self->openItem(item);
                });
                button->m_scaleMultiplier = 1.04f;
                int col = index % kColumns, row = index / kColumns;
                button->setPosition({left + kCellW / 2.f + col * (kCellW + 10.f), 200.f - row * (kCellH + 34.f)});
                self->m_grid->addChild(button);
                index++;
            }
        });
}

void CommunityBackgroundsPopup::openItem(matjson::Value const& item) {
    if (m_onPick) {
        auto callback = m_onPick;
        auto id = item["id"].asString().unwrapOr("");
        auto title = item["title"].asString().unwrapOr("");
        onClose(nullptr);
        callback(id, title);
        return;
    }
    WeakRef<CommunityBackgroundsPopup> weak = this;
    auto* popup = CommunityItemPopup::create(item, [weak] {
        if (auto self = weak.lock()) self->load();
    });
    if (popup) showOnTop(popup);
}

void useBackgroundForComments(std::string const& id, std::function<void(bool ok)> done) {
    auto identity = currentIdentity();
    if (!identity) {
        PaimonNotify::show(tx("Inicia sesion en Geometry Dash.", "Log in to Geometry Dash."), NotificationIcon::Warning);
        if (done) done(false);
        return;
    }
    int accountID = identity->accountID;
    // keep every other profile setting: only edit a config that was actually read.
    ThumbnailAPI::get().downloadProfileConfig(accountID, [id, accountID, done](bool ok, ProfileConfig const& current) {
        if (!ok) {
            PaimonNotify::show(tx("No se pudo leer tu perfil. Reintenta.", "Could not read your profile. Try again."), NotificationIcon::Error);
            if (done) done(false);
            return;
        }
        ProfileConfig next = current;
        next.commentBgType = "community";
        next.commentBgCommunityId = id;
        ThumbnailAPI::get().uploadProfileConfig(accountID, next, [done](bool saved, std::string const& msg) {
            if (saved) {
                PaimonNotify::show(tx("Fondo de comentarios actualizado.", "Comment background updated."), NotificationIcon::Success);
                if (done) done(true);
                return;
            }
            if (needsVerification(msg)) promptVerification();
            PaimonNotify::show(describeError(msg).c_str(), NotificationIcon::Error);
            if (done) done(false);
        });
    });
}

} // namespace paimon::community
