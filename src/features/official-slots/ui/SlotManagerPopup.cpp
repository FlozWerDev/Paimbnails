#include "SlotManagerPopup.hpp"
#include "SlotEditorPopup.hpp"
#include "SlotOrderPopup.hpp"
#include "SlotVisuals.hpp"
#include "../services/OfficialSlotStore.hpp"
#include "../services/SlotLevels.hpp"
#include "../services/SlotListRefresh.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/LevelInfoLayer.hpp>
#include <Geode/loader/Loader.hpp>
#include <fmt/format.h>
#include <algorithm>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::officialslots::ui {
namespace {
constexpr float kWidth = 520.f;
constexpr float kHeight = 340.f;
constexpr float kListY = 52.f;
constexpr float kRowHeight = 70.f;
constexpr float kRowGap = 5.f;

std::string tr(char const* key) { return Localization::get().getString(key); }

std::optional<Slot> pageSlot(std::string const& key) {
    auto& store = SlotStore::get();
    int officialId = 0;
    if (SlotStore::officialKeyId(key, officialId)) return store.replacementForOfficial(officialId);
    return key.starts_with("s:") ? store.find(key.substr(2)) : std::nullopt;
}

std::string pageName(std::string const& key) {
    if (auto slot = pageSlot(key)) return slotDisplayName(*slot);
    int officialId = 0;
    return SlotStore::officialKeyId(key, officialId) ? officialName(officialId) : key;
}
}

SlotManagerPopup* SlotManagerPopup::create(std::function<void()> onChanged) {
    auto* popup = new SlotManagerPopup();
    if (popup->init(std::move(onChanged))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool SlotManagerPopup::init(std::function<void()> onChanged) {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    m_onChanged = std::move(onChanged);
    this->setTitle(tr("slot.manager.title"));
    this->addInfoButton(tr("slot.manager.title"),
        "La lista incluye todos los niveles, en el orden del selector. "
        "<cg>Editar</c> cambia el nivel de esa pagina. Las flechas mueven oficiales y personalizados. "
        "<cy>Ocultar</c> retira la pagina sin dejar huecos; <cg>Mostrar</c> recupera su posicion. "
        "<cr>Restaurar</c> quita un reemplazo y recupera el original.");
    m_summary = CCLabelBMFont::create("", "chatFont.fnt");
    m_summary->setPosition({kWidth / 2.f, kHeight - 43.f});
    m_summary->setScale(0.55f);
    m_mainLayer->addChild(m_summary, 3);
    this->buildList();
    this->buildFooter();
    this->rebuild();
    return true;
}

void SlotManagerPopup::buildList() {
    float const height = kHeight - kListY - 62.f;
    auto* panel = paimon::ui::makeInset({kWidth - 24.f, height + 6.f}, 100);
    panel->setPosition({12.f, kListY - 3.f});
    m_mainLayer->addChild(panel, 1);
    m_scroll = ScrollLayer::create(CCSize{kWidth - 30.f, height});
    if (!m_scroll) return;
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({15.f, kListY});
    m_scroll->setStealingTouches(true);
    m_scroll->setID("manager-level-list"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

void SlotManagerPopup::buildFooter() {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    m_mainLayer->addChild(menu, 3);
    auto* add = paimon::ui::makeButton(fmt::format("+ {}", tr("slot.manager.add")).c_str(),
        [this] { this->openEditor(); }, paimon::ui::Btn::Green, 130.f, 0.65f, "bigFont.fnt");
    add->setPosition({180.f, 25.f});
    menu->addChild(add);
    auto* order = paimon::ui::makeButton(tr("slot.manager.order").c_str(),
        [this] { this->onReorder(); }, paimon::ui::Btn::Cyan, 130.f, 0.65f, "bigFont.fnt");
    order->setPosition({340.f, 25.f});
    menu->addChild(order);
}

void SlotManagerPopup::rebuild() {
    if (!m_scroll) return;
    auto* content = m_scroll->m_contentLayer;
    float const offset = content->getPositionY();
    content->removeAllChildren();
    auto& store = SlotStore::get();
    auto const& order = store.pageOrder();
    float const viewport = m_scroll->getContentSize().height;
    float const width = m_scroll->getContentSize().width - 4.f;
    float const height = std::max(viewport, order.size() * (kRowHeight + kRowGap) - kRowGap);
    content->setContentHeight(height);
    float y = height;
    for (auto const& key : order) {
        auto* row = this->buildPageRow(key, width);
        y -= kRowHeight;
        row->setPosition({2.f, y});
        content->addChild(row);
        y -= kRowGap;
    }
    if (m_listBuilt) content->setPositionY(std::clamp(offset, viewport - height, 0.f));
    else m_scroll->scrollToTop();
    m_listBuilt = true;
    m_summary->setString(fmt::format(fmt::runtime(tr("slot.manager.summary")),
        store.visiblePages().size(), order.size()).c_str());
}

CCNode* SlotManagerPopup::buildPageRow(std::string const& key, float width) {
    auto& store = SlotStore::get();
    auto slot = pageSlot(key);
    bool const visible = store.pageVisible(key);
    int officialId = 0;
    bool const official = SlotStore::officialKeyId(key, officialId);
    auto* row = CCNode::create();
    row->setContentSize({width, kRowHeight});
    row->setID("catalogue-row-" + key);
    row->addChild(paimon::ui::makeInset({width, kRowHeight}, visible ? 85 : 145), -1);
    Difficulty difficulty = Difficulty::Unrated;
    Tier tier = Tier::None;
    if (slot) {
        difficulty = slot->difficulty;
        tier = slot->tier;
    } else if (auto* level = GameLevelManager::get()->getMainLevel(officialId, true)) {
        difficulty = level->m_demon.value() > 0 ? Difficulty::Demon
            : static_cast<Difficulty>(std::clamp(static_cast<int>(level->m_difficulty), 0, 5));
    }
    if (auto* badge = createDifficultyBadge(difficulty, tier, 0.5f)) {
        badge->setPosition({25.f, 47.f});
        row->addChild(badge);
    }
    auto* title = paimon::ui::makeLabel(pageName(key).c_str(), width - 125.f, 0.45f,
        visible ? ccColor3B{255, 255, 255} : ccColor3B{155, 155, 165});
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({52.f, 52.f});
    row->addChild(title);

    std::string detail = slot
        ? (slot->source == Source::LevelId ? fmt::format("ID {}", slot->levelId) : ".gmd")
        : fmt::format(fmt::runtime(tr("slot.manager.official")), officialId);
    if (slot && !slot->author.empty()) detail += " - " + slot->author;
    if (slot && official) detail += " - " + officialName(officialId);
    if (!visible) detail += " - " + tr("slot.manager.hidden_state");
    auto* subtitle = paimon::ui::makeLabel(detail.c_str(), width - 70.f, 0.3f, {190, 195, 210});
    subtitle->setAnchorPoint({0.f, 0.5f});
    subtitle->setPosition({52.f, 36.f});
    row->addChild(subtitle);
    auto const position = store.visiblePosition(key);
    auto const positionText = position ? fmt::format("#{}", position) : "--";
    auto* number = paimon::ui::makeLabel(positionText.c_str(), 55.f, 0.4f, {140, 235, 155});
    number->setAnchorPoint({1.f, 0.5f});
    number->setPosition({width - 10.f, 52.f});
    row->addChild(number);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowHeight});
    row->addChild(menu, 3);
    float x = 8.f;
    auto button = [&](std::string const& text, float buttonWidth, paimon::ui::Btn skin, auto action) {
        auto* item = paimon::ui::makeButton(text.c_str(), action, skin, buttonWidth, 0.43f, "bigFont.fnt");
        item->setPosition({x + buttonWidth / 2.f, 14.f});
        menu->addChild(item);
        x += buttonWidth + 6.f;
    };
    button(tr("slot.manager.test"), 66.f, paimon::ui::Btn::Cyan, [this, key] { this->onPlay(key); });
    button(tr("slot.manager.edit"), 66.f, paimon::ui::Btn::Green, [this, key] { this->openEditor(key); });
    button("^", 28.f, paimon::ui::Btn::Gray, [this, key] { this->onMove(key, -1); });
    button("v", 28.f, paimon::ui::Btn::Gray, [this, key] { this->onMove(key, 1); });
    button(tr(visible ? "slot.manager.hide" : "slot.manager.show"), 80.f,
        paimon::ui::Btn::Pink, [this, key] { this->onToggle(key); });
    if (slot) {
        button(tr(official ? "slot.manager.restore" : "slot.manager.delete"), 98.f,
            paimon::ui::Btn::Red, [this, key] { this->onDelete(key); });
    }
    return row;
}

void SlotManagerPopup::openEditor(std::string const& key) {
    auto slot = pageSlot(key);
    int officialId = 0;
    SlotStore::officialKeyId(key, officialId);
    WeakRef<SlotManagerPopup> weak(this);
    auto* editor = SlotEditorPopup::create(slot ? std::optional(slot->id) : std::nullopt, officialId, [weak] {
        if (auto self = weak.lock(); self && self->getParent()) self->mutated();
    });
    if (editor) editor->show();
}

void SlotManagerPopup::onPlay(std::string const& key) {
    if (auto slot = pageSlot(key)) {
        openSlotLevel(*slot);
        return;
    }
    int officialId = 0;
    if (!SlotStore::officialKeyId(key, officialId)) return;
    auto* level = GameLevelManager::get()->getMainLevel(officialId, false);
    if (!level) return;
    if (auto* scene = LevelInfoLayer::scene(level, false)) {
        CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
    }
}

void SlotManagerPopup::onMove(std::string const& key, int delta) {
    SlotStore::get().movePage(key, delta);
    this->mutated();
}

void SlotManagerPopup::onToggle(std::string const& key) {
    auto& store = SlotStore::get();
    int officialId = 0;
    if (SlotStore::officialKeyId(key, officialId)) {
        store.setOfficialHidden(officialId, !store.isOfficialHidden(officialId));
    } else if (auto slot = pageSlot(key)) {
        slot->enabled = !slot->enabled;
        store.update(*slot);
    }
    this->mutated();
}

void SlotManagerPopup::onDelete(std::string const& key) {
    auto slot = pageSlot(key);
    if (!slot) return;
    WeakRef<SlotManagerPopup> weak(this);
    createQuickPopup(tr("slot.manager.delete_title").c_str(),
        fmt::format(fmt::runtime(tr("slot.manager.delete_body")), slotDisplayName(*slot)),
        tr("general.cancel").c_str(), tr("general.ok").c_str(),
        [weak, id = slot->id](FLAlertLayer*, bool confirmed) {
            auto self = weak.lock();
            if (!confirmed || !self || !self->getParent()) return;
            if (SlotStore::get().remove(id)) {
                SlotLevelCache::get().invalidate(id);
                self->mutated();
            }
        });
}

void SlotManagerPopup::onReorder() {
    WeakRef<SlotManagerPopup> weak(this);
    if (auto* popup = SlotOrderPopup::create([weak] {
            if (auto self = weak.lock(); self && self->getParent()) self->mutated();
        })) popup->show();
}

void SlotManagerPopup::mutated() {
    WeakRef<SlotManagerPopup> weak(this);
    Loader::get()->queueInMainThread([weak] {
        if (auto self = weak.lock(); self && self->getParent()) self->rebuild();
    });
    refreshOfficialList();
    if (m_onChanged) m_onChanged();
}

} // namespace paimon::officialslots::ui
