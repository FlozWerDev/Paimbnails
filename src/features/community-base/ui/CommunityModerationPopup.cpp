#include "CommunityModerationPopup.hpp"
#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/CommunityItems.hpp"
#include "../services/CommunityMediaCache.hpp"
#include "../../global-icon/GlobalIconTypes.hpp"
#include "../../paimon-shop/data/ShopPreset.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <array>
#include <memory>

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr std::array<char const*, 4> kTypes = {"presets", "gradients", "icons", "backgrounds"};
constexpr int kPreviewShaderSlot = 30;

void enable(CCMenuItemSpriteExtra* button, bool on) {
    if (!button) return;
    button->setEnabled(on);
    button->setOpacity(on ? 255 : 90);
}
} // namespace

CommunityModerationPopup* CommunityModerationPopup::create() {
    auto popup = std::make_unique<CommunityModerationPopup>();
    if (!popup->init()) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool CommunityModerationPopup::init() {
    constexpr float W = 440.f, H = 290.f;
    if (!PaimonPopup::init(W, H)) return false;
    setTitle(tx("Moderacion de Paimon Base", "Paimon Base moderation"), "goldFont.fnt", 0.55f);
    addInfoButton("Paimon Base", tx(
        "Revisa lo que envian los jugadores. Lo <cg>aprobado</c> aparece para todos en Paimon Shop. "
        "Lo <cr>rechazado</c> no se publica, se liberan sus archivos y el autor ve el motivo en Mis envios.",
        "Review what players send. <cg>Approved</c> items show up for everyone in Paimon Shop. "
        "<cr>Rejected</c> items are not published, their files are released and the author sees the reason in My submissions."));
    paimon::markDynamicPopup(this);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 4);
    float const tabW = (W - 40.f) / kTypes.size();
    for (size_t i = 0; i < kTypes.size(); ++i) {
        std::string id = kTypes[i];
        auto* tab = paimon::ui::makeButton(items::typeName(id), [this, id] { m_type = id; load(); },
            paimon::ui::Btn::Gray, tabW - 6.f, 0.36f, "bigFont.fnt");
        tab->setPosition({20.f + tabW / 2.f + i * tabW, H - 48.f});
        menu->addChild(tab);
        m_tabs.emplace_back(id, tab);
    }

    m_card = CCNode::create();
    m_mainLayer->addChild(m_card, 2);
    m_counter = paimon::ui::makeLabel("", 200.f, 0.3f, paimon::ui::palette::muted);
    m_counter->setPosition({W / 2.f, 56.f});
    m_mainLayer->addChild(m_counter);

    auto* prev = paimon::ui::makeButton("<", [this] {
        if (m_index > 0) { --m_index; showCurrent(); }
    }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    prev->setPosition({30.f, 26.f});
    menu->addChild(prev);
    auto* next = paimon::ui::makeButton(">", [this] {
        if (m_index + 1 < m_items.size()) { ++m_index; showCurrent(); }
    }, paimon::ui::Btn::Gray, 34.f, 0.45f);
    next->setPosition({W - 30.f, 26.f});
    menu->addChild(next);
    m_approve = paimon::ui::makeButton(tx("Aprobar", "Approve"), [this] { decide(true, ""); },
        paimon::ui::Btn::Green, 110.f, 0.45f, "bigFont.fnt");
    m_approve->setPosition({W / 2.f - 64.f, 26.f});
    menu->addChild(m_approve);
    m_reject = paimon::ui::makeButton(tx("Rechazar", "Reject"), [this] {
        WeakRef<CommunityModerationPopup> weak = this;
        auto* prompt = TextPromptPopup::create(tx("Motivo del rechazo", "Rejection reason"),
            tx("Lo vera el autor", "The author will see it"), 200, false, [weak](std::string const& reason) {
                if (auto self = weak.lock()) self->decide(false, reason);
            });
        showOnTop(prompt);
    }, paimon::ui::Btn::Red, 110.f, 0.45f, "bigFont.fnt");
    m_reject->setPosition({W / 2.f + 64.f, 26.f});
    menu->addChild(m_reject);
    loadSummary();
    load();
    return true;
}

// pending counts on the tabs.
void CommunityModerationPopup::loadSummary() {
    WeakRef<CommunityModerationPopup> weak = this;
    CommunityClient::get().moderationSummary([weak](bool ok, matjson::Value const& json, std::string const&) {
        auto self = weak.lock();
        if (!self || !ok) return;
        for (auto const& [id, tab] : self->m_tabs) {
            auto pending = json[id].asInt().unwrapOr(0);
            auto label = pending > 0 ? fmt::format("{} ({})", items::typeName(id), pending) : std::string(items::typeName(id));
            if (auto* sprite = typeinfo_cast<ButtonSprite*>(tab->getNormalImage())) sprite->setString(label.c_str());
        }
    });
}

void CommunityModerationPopup::load() {
    int token = ++m_token;
    for (auto const& [id, tab] : m_tabs) {
        paimon::ui::setButtonSkin(tab, id == m_type ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
    m_items = matjson::Value::array();
    m_index = 0;
    showCurrent();
    m_counter->setString(tx("Cargando...", "Loading..."));
    WeakRef<CommunityModerationPopup> weak = this;
    CommunityClient::get().moderationQueue(m_type, 0, [weak, token](bool ok, matjson::Value const& json, std::string const& error) {
        auto self = weak.lock();
        if (!self || token != self->m_token) return;
        if (!ok) { self->m_counter->setString(error.c_str()); return; }
        self->m_items = json["items"].isArray() ? json["items"] : matjson::Value::array();
        self->m_index = 0;
        self->showCurrent();
    });
}

void CommunityModerationPopup::showCurrent() {
    m_card->removeAllChildren();
    bool const has = m_index < m_items.size();
    enable(m_approve, has && !m_busy);
    enable(m_reject, has && !m_busy);
    if (!has) {
        m_counter->setString(tx("No hay nada pendiente.", "Nothing is pending."));
        return;
    }
    matjson::Value const& item = m_items[m_index];
    m_counter->setString(fmt::format("{} / {}", m_index + 1, m_items.size()).c_str());
    auto* preview = makeItemPreview(item, {220.f, 140.f}, kPreviewShaderSlot, true);
    preview->setPosition({20.f, 76.f});
    m_card->addChild(preview);
    auto addLabel = [this](std::string const& text, float y, float scale, ccColor3B color) {
        auto* label = paimon::ui::makeLabel(text.c_str(), 170.f, scale, color);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({252.f, y});
        m_card->addChild(label);
    };
    addLabel(item["title"].asString().unwrapOr(""), 206.f, 0.38f, paimon::ui::palette::text);
    addLabel(fmt::format("{} {}", tx("por", "by"), item["username"].asString().unwrapOr("")), 186.f, 0.32f, paimon::ui::palette::gold);
    if (m_type == "presets") {
        auto category = shop::parseCategory(item["category"].asString().unwrapOr(""));
        addLabel(category ? shop::categoryName(category.unwrap()) : "?", 168.f, 0.3f, paimon::ui::palette::info);
        addLabel(fmt::format("{} KB", item["configBytes"].asInt().unwrapOr(0) / 1024), 152.f, 0.28f, paimon::ui::palette::muted);
    } else if (m_type == "gradients" || m_type == "icons") {
        auto iconType = paimon::globalicon::iconTypeFromString(item["iconType"].asString().unwrapOr(""));
        addLabel(iconType ? items::gamemodeName(*iconType) : "?", 168.f, 0.3f, paimon::ui::palette::info);
        if (m_type == "icons") {
            auto const& icon = item["icon"];
            addLabel(fmt::format("{}  |  {}", icon["name"].asString().unwrapOr(""),
                icon["plistUrl"].asString().unwrapOr("").empty() ? "PNG" : "PNG + plist"), 152.f, 0.26f, paimon::ui::palette::muted);
        }
    }
    auto description = item["description"].asString().unwrapOr("");
    if (!description.empty()) {
        auto* text = paimon::ui::makeText(description.c_str(), 170.f, 0.36f);
        text->setAnchorPoint({0.f, 1.f});
        text->setPosition({252.f, 140.f});
        if (text->getScaledContentSize().height > 60.f) text->setScale(text->getScale() * 60.f / text->getScaledContentSize().height);
        m_card->addChild(text);
    }
}

void CommunityModerationPopup::decide(bool approve, std::string const& reason) {
    if (m_busy || m_index >= m_items.size()) return;
    m_busy = true;
    showCurrent();
    matjson::Value const& current = m_items[m_index];
    auto id = current["id"].asString().unwrapOr("");
    auto mediaPath = current["mediaPath"].asString().unwrapOr("");
    WeakRef<CommunityModerationPopup> weak = this;
    CommunityClient::get().review(m_type, id, approve, reason,
        [weak, approve, mediaPath](bool ok, matjson::Value const&, std::string const& error) {
            auto self = weak.lock();
            if (!self) return;
            self->m_busy = false;
            if (!ok) {
                PaimonNotify::show(error, NotificationIcon::Error);
                self->showCurrent();
                return;
            }
            if (!mediaPath.empty()) CommunityMediaCache::get().forget(mediaPath);
            PaimonNotify::show(approve ? tx("Aprobado.", "Approved.") : tx("Rechazado.", "Rejected."), NotificationIcon::Success);
            auto remaining = matjson::Value::array();
            for (size_t i = 0; i < self->m_items.size(); ++i) {
                if (i != self->m_index) remaining.push(self->m_items[i]);
            }
            self->m_items = remaining;
            self->loadSummary();
            // the queue comes in small pages: fetch the next one when this one is done.
            if (self->m_items.size() == 0) {
                self->load();
                return;
            }
            if (self->m_index >= self->m_items.size() && self->m_index > 0) self->m_index--;
            self->showCurrent();
        });
}

} // namespace paimon::community
