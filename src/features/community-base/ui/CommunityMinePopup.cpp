#include "CommunityMinePopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/CommunityItems.hpp"
#include "../services/CommunityMediaCache.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <array>
#include <memory>

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr float W = 420.f;
constexpr float H = 270.f;
constexpr size_t kRows = 6;
constexpr std::array<char const*, 4> kTypes = {"presets", "gradients", "icons", "backgrounds"};

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}
} // namespace

CommunityMinePopup* CommunityMinePopup::create(std::string const& type) {
    auto popup = std::make_unique<CommunityMinePopup>();
    if (!popup->init(type)) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool CommunityMinePopup::init(std::string const& type) {
    if (!PaimonPopup::init(W, H)) return false;
    setTitle(tx("Mis envios", "My submissions"), "goldFont.fnt", 0.6f);
    addInfoButton("Paimon Base", tx(
        "Todo lo que enviaste a Paimon Base. <cy>Pendiente</c>: esperando a un moderador. "
        "<cg>Aprobado</c>: visible en Paimon Shop. <cr>Rechazado</c>: no se publica; abajo ves el motivo.\n\n"
        "Borrar quita tambien sus votos y descargas.",
        "Everything you sent to Paimon Base. <cy>Pending</c>: waiting for a moderator. "
        "<cg>Approved</c>: visible in Paimon Shop. <cr>Rejected</c>: not published; the reason shows below it.\n\n"
        "Deleting also removes its votes and downloads."));
    paimon::markDynamicPopup(this);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 4);
    float const tabW = (W - 40.f) / kTypes.size();
    for (size_t i = 0; i < kTypes.size(); ++i) {
        std::string id = kTypes[i];
        auto* tab = paimon::ui::makeButton(items::typeName(id), [this, id] { selectType(id); },
            paimon::ui::Btn::Gray, tabW - 6.f, 0.36f, "bigFont.fnt");
        tab->setPosition({20.f + tabW / 2.f + i * tabW, H - 48.f});
        menu->addChild(tab);
        m_tabs.emplace_back(id, tab);
    }
    m_prev = paimon::ui::makeButton("<", [this] { if (m_page > 0) { --m_page; showPage(); } }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    m_prev->setPosition({30.f, 24.f});
    menu->addChild(m_prev);
    m_next = paimon::ui::makeButton(">", [this] { ++m_page; showPage(); }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    m_next->setPosition({W - 30.f, 24.f});
    menu->addChild(m_next);
    m_list = CCNode::create();
    m_mainLayer->addChild(m_list, 2);

    bool known = false;
    for (auto const* id : kTypes) known = known || type == id;
    selectType(known ? type : "presets");
    return true;
}

void CommunityMinePopup::selectType(std::string const& type) {
    m_type = type;
    for (auto const& [id, tab] : m_tabs) {
        paimon::ui::setButtonSkin(tab, id == m_type ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    load();
}

void CommunityMinePopup::load() {
    int token = ++m_token;
    m_items = matjson::Value::array();
    m_page = 0;
    m_list->removeAllChildren();
    enable(m_prev, false);
    enable(m_next, false);
    auto* loading = paimon::ui::makeLabel(tx("Cargando...", "Loading..."), W - 40.f, 0.36f, paimon::ui::palette::muted);
    loading->setPosition({W / 2.f, 120.f});
    m_list->addChild(loading);
    WeakRef<CommunityMinePopup> weak = this;
    CommunityClient::get().myItems(m_type, [weak, token](bool ok, matjson::Value const& json, std::string const& error) {
        auto self = weak.lock();
        if (!self || token != self->m_token) return;
        if (!ok) {
            self->m_list->removeAllChildren();
            auto* failed = paimon::ui::makeLabel(error.c_str(), W - 40.f, 0.34f, paimon::ui::palette::warning);
            failed->setPosition({W / 2.f, 120.f});
            self->m_list->addChild(failed);
            return;
        }
        self->m_items = json["items"].isArray() ? json["items"] : matjson::Value::array();
        self->showPage();
    });
}

void CommunityMinePopup::showPage() {
    m_list->removeAllChildren();
    if (m_items.size() == 0) {
        auto* empty = paimon::ui::makeLabel(tx("Todavia no enviaste nada de este tipo.", "You have not sent anything of this type yet."),
            W - 40.f, 0.36f, paimon::ui::palette::muted);
        empty->setPosition({W / 2.f, 120.f});
        m_list->addChild(empty);
        enable(m_prev, false);
        enable(m_next, false);
        return;
    }
    size_t const pages = (m_items.size() + kRows - 1) / kRows;
    m_page = std::min(m_page, pages - 1);
    enable(m_prev, m_page > 0);
    enable(m_next, m_page + 1 < pages);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_list->addChild(menu);
    float y = H - 82.f;
    for (size_t i = m_page * kRows; i < std::min(m_items.size(), (m_page + 1) * kRows); ++i) {
        matjson::Value const& item = m_items[i];
        auto id = item["id"].asString().unwrapOr("");
        auto title = item["title"].asString().unwrapOr("");
        auto status = item["status"].asString().unwrapOr("pending");
        auto reason = item["rejectReason"].asString().unwrapOr("");
        auto* row = paimon::ui::makeInset({W - 40.f, 26.f}, 90);
        row->setPosition({20.f, y - 13.f});
        m_list->addChild(row);
        auto* name = paimon::ui::makeLabel(title.c_str(), 150.f, 0.32f, paimon::ui::palette::text);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({30.f, reason.empty() ? y : y + 5.f});
        m_list->addChild(name);
        if (!reason.empty()) {
            auto* why = paimon::ui::makeLabel(reason.c_str(), 240.f, 0.22f, paimon::ui::palette::danger);
            why->setAnchorPoint({0.f, 0.5f});
            why->setPosition({30.f, y - 6.f});
            m_list->addChild(why);
        }
        auto* state = paimon::ui::makeLabel(statusLabel(status), 80.f, 0.28f, statusColor(status));
        state->setAnchorPoint({0.f, 0.5f});
        state->setPosition({186.f, y});
        m_list->addChild(state);
        if (status == "approved") {
            auto* counters = makeCounters(static_cast<int>(item["likes"].asInt().unwrapOr(0)),
                static_cast<int>(item["dislikes"].asInt().unwrapOr(0)), static_cast<int>(item["downloads"].asInt().unwrapOr(0)), 0.26f);
            if (counters->getContentWidth() > 96.f) counters->setScale(96.f / counters->getContentWidth());
            counters->setPosition({262.f, y});
            m_list->addChild(counters);
        }
        auto* removeButton = paimon::ui::makeButton(tx("Borrar", "Delete"), [this, id, title] { remove(id, title); },
            paimon::ui::Btn::Red, 50.f, 0.3f, "bigFont.fnt");
        removeButton->setPosition({W - 48.f, y});
        menu->addChild(removeButton);
        y -= 30.f;
    }
}

void CommunityMinePopup::remove(std::string const& id, std::string const& title) {
    WeakRef<CommunityMinePopup> weak = this;
    auto type = m_type;
    auto* confirm = createQuickPopup("Paimon Base",
        fmt::format("{} <cy>{}</c>?", tx("Borrar", "Delete"), title),
        tx("Cancelar", "Cancel"), tx("Borrar", "Delete"), [weak, id, type](auto*, bool confirmed) {
            if (!confirmed) return;
            CommunityClient::get().deleteItem(type, id, [weak, id, type](bool ok, matjson::Value const&, std::string const& error) {
                if (!ok) { PaimonNotify::show(error, NotificationIcon::Error); return; }
                PaimonNotify::show(tx("Borrado.", "Deleted."), NotificationIcon::Success);
                if (type == "backgrounds") CommunityMediaCache::get().forget(CommunityClient::backgroundUrl(id));
                if (auto self = weak.lock()) self->load();
            });
        }, false);
    showOnTop(confirm);
}

} // namespace paimon::community
