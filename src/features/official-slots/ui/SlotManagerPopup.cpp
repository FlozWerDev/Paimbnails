#include "SlotManagerPopup.hpp"

#include "SlotEditorPopup.hpp"
#include "SlotOrderPopup.hpp"
#include "SlotVisuals.hpp"
#include "../services/OfficialSlotStore.hpp"
#include "../services/SlotLevels.hpp"
#include "../services/SlotListRefresh.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/loader/Loader.hpp>

#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::officialslots::ui {

namespace {

constexpr float kWidth = 440.f;
constexpr float kHeight = 300.f;
constexpr float kRowGap = 6.f;
constexpr float kListY = 46.f;

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

void toast(std::string const& text, NotificationIcon icon) {
    PaimonNotify::show(text, icon);
}

std::string officialName(int officialId) {
    if (auto* glm = GameLevelManager::get()) {
        if (auto* main = glm->getMainLevel(officialId, true)) {
            std::string name = main->m_levelName.c_str();
            if (!name.empty()) return name;
        }
    }
    return fmt::format("#{}", officialId);
}

std::string slotDisplayName(Slot const& slot) {
    if (!slot.name.empty()) return slot.name;
    if (slot.source == Source::LevelId && slot.levelId > 0) {
        return fmt::format("Level #{}", slot.levelId);
    }
    return tr("slot.level.unnamed");
}

std::string slotSourceText(Slot const& slot) {
    if (slot.source == Source::LevelId) {
        return fmt::format("ID {}", slot.levelId);
    }
    return slot.gmdFile.empty() ? ".gmd" : slot.gmdFile;
}

ButtonSprite* smallButton(char const* text, int width, char const* bg = "GJ_button_01.png") {
    return ButtonSprite::create(text, width, true, "bigFont.fnt", bg, 18.f, 0.45f);
}

} // namespace

SlotManagerPopup* SlotManagerPopup::create(std::function<void()> onChanged) {
    auto* ret = new SlotManagerPopup();
    if (ret && ret->init(std::move(onChanged))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SlotManagerPopup::init(std::function<void()> onChanged) {
    if (!Popup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);
    m_onChanged = std::move(onChanged);
    this->setTitle(tr("slot.manager.title"));
    this->buildHeader();
    this->buildList();
    this->buildFooter();
    this->rebuild();
    return true;
}

void SlotManagerPopup::buildHeader() {
    auto* menu = CCMenu::create();
    menu->setPosition({kWidth - 52.f, kHeight - 32.f});
    menu->setContentSize({92.f, 30.f});
    menu->setID("manager-add-menu"_spr);
    m_mainLayer->addChild(menu, 3);

    auto* spr = smallButton(fmt::format("+ {}", tr("slot.manager.add")).c_str(), 88);
    if (!spr) return;
    auto* item = CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra*) {
        this->onAdd(nullptr);
    });
    if (!item) return;
    menu->addChild(item);
}

void SlotManagerPopup::buildList() {
    float const scrollH = kHeight - 134.f;
    float const scrollW = kWidth - 30.f;
    if (auto* panel = CCScale9Sprite::create("GJ_square02.png")) {
        panel->setContentSize({kWidth - 20.f, scrollH + 6.f});
        panel->setPosition({kWidth / 2.f, kListY + scrollH / 2.f});
        panel->setOpacity(220);
        panel->setID("manager-list-bg"_spr);
        m_mainLayer->addChild(panel, 1);
    }

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({15.f, kListY});
    m_scroll->m_contentLayer->setID("manager-scroll-content"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

void SlotManagerPopup::buildFooter() {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("manager-footer-menu"_spr);
    m_mainLayer->addChild(menu, 3);

    if (auto* spr = smallButton(tr("slot.manager.order").c_str(), 72)) {
        auto* item = CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra*) {
            this->onReorder();
        });
        item->setPosition({kWidth / 2.f, 24.f});
        menu->addChild(item);
    }
}

void SlotManagerPopup::rebuild() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    float const rowW = m_scroll->getContentSize().width - 4.f;
    std::vector<CCNode*> rows;

    for (auto const& slot : SlotStore::get().slots()) {
        if (auto* row = this->buildSlotRow(slot, rowW)) rows.push_back(row);
    }
    auto const& hidden = SlotStore::get().hiddenOfficials();
    if (!hidden.empty()) {
        rows.push_back(this->buildSectionLabel("slot.manager.hidden", rowW));
        for (int id : hidden) {
            if (auto* row = this->buildHiddenRow(id, rowW)) rows.push_back(row);
        }
    }

    float totalH = 0.f;
    for (auto* row : rows) totalH += row->getContentSize().height + kRowGap;
    if (!rows.empty()) totalH -= kRowGap;
    totalH = std::max(totalH, m_scroll->getContentSize().height);

    if (rows.empty()) {
        if (auto* label = CCLabelBMFont::create(tr("slot.manager.empty").c_str(), "chatFont.fnt")) {
            label->setScale(0.55f);
            label->setPosition(m_scroll->getContentSize() / 2.f);
            label->setColor({200, 200, 220});
            m_scroll->m_contentLayer->addChild(label);
        }
    } else {
        float y = totalH;
        for (auto* row : rows) {
            float const h = row->getContentSize().height;
            y -= h;
            row->setPosition({2.f, y});
            m_scroll->m_contentLayer->addChild(row);
            y -= kRowGap;
        }
    }
    m_scroll->m_contentLayer->setContentHeight(totalH);
    m_scroll->scrollToTop();
}

CCNode* SlotManagerPopup::buildSectionLabel(char const* key, float width) {
    auto* row = CCNode::create();
    row->setContentSize({width, 22.f});
    if (auto* label = CCLabelBMFont::create(tr(key).c_str(), "goldFont.fnt")) {
        label->setScale(0.5f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({6.f, 11.f});
        row->addChild(label);
    }
    return row;
}

CCNode* SlotManagerPopup::buildSlotRow(Slot const& slot, float width) {
    constexpr float kRowH = 88.f;

    auto* row = CCNode::create();
    row->setContentSize({width, kRowH});
    if (auto* bg = createCardBackground({width, kRowH - 2.f})) {
        bg->setPosition({width / 2.f, kRowH / 2.f});
        row->addChild(bg, -1);
    }

    if (auto* badge = createDifficultyBadge(slot.difficulty, slot.tier, 0.6f)) {
        badge->setPosition({32.f, 60.f});
        row->addChild(badge);
    }

    std::string name = slotDisplayName(slot);
    if (auto* nameLbl = CCLabelBMFont::create(name.c_str(), "bigFont.fnt")) {
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({60.f, 68.f});
        nameLbl->limitLabelWidth(220.f, 0.5f, 0.1f);
        row->addChild(nameLbl);
    }

    std::string sub = fmt::format("{} - {} - {}*",
        slot.author.empty() ? "?" : slot.author, slotSourceText(slot), slot.stars);
    if (slot.replacesOfficialId != 0) {
        sub += fmt::format(fmt::runtime(" -> " + tr("slot.editor.replaces")),
                           officialName(slot.replacesOfficialId));
    }
    if (auto* subLbl = CCLabelBMFont::create(sub.c_str(), "bigFont.fnt")) {
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setPosition({60.f, 50.f});
        subLbl->setColor({170, 180, 200});
        subLbl->limitLabelWidth(240.f, 0.38f, 0.1f);
        row->addChild(subLbl);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowH});
    menu->setID("slot-row-menu"_spr);
    row->addChild(menu, 1);

    std::string const onOff = slot.enabled ? tr("slot.manager.on") : tr("slot.manager.off");
    // Order matches the row: play, edit, reorder, toggle, delete.
    std::vector<std::tuple<std::string, int, char const*, int>> defs = {
        {tr("slot.manager.test"), 62, "GJ_button_01.png", 0},
        {tr("slot.manager.edit"), 58, "GJ_button_01.png", 1},
        {"^", 30, "GJ_button_02.png", 2},
        {"v", 30, "GJ_button_02.png", 3},
        {onOff, 66, "GJ_button_05.png", 4},
        {"X", 30, "GJ_button_06.png", 5},
    };
    float totalW = 0.f;
    for (auto const& [text, w, bg, action] : defs) totalW += w + 6.f;
    totalW -= 6.f;
    float x = (width - totalW) / 2.f;
    std::string const slotId = slot.id;
    Slot slotCopy = slot;
    for (auto const& [text, w, bg, action] : defs) {
        auto* spr = smallButton(text.c_str(), w, bg);
        if (!spr) continue;
        auto* item = CCMenuItemExt::createSpriteExtra(
            spr, [this, slotId, slotCopy, action](CCMenuItemSpriteExtra*) {
                switch (action) {
                    case 0: this->onTestSlot(slotCopy); break;
                    case 1: this->onEditSlot(slotId); break;
                    case 2: this->onMoveSlot(slotId, -1); break;
                    case 3: this->onMoveSlot(slotId, 1); break;
                    case 4: this->onToggleSlot(slotId); break;
                    default: this->onDeleteSlot(slotCopy); break;
                }
            });
        if (!item) continue;
        item->setPosition({x + w / 2.f, 20.f});
        menu->addChild(item);
        x += w + 6.f;
    }
    // Disabled rows keep their layout and say Inactivo on the toggle; dimming
    // the whole row would need an RGBA container, so the label does the job.
    return row;
}

CCNode* SlotManagerPopup::buildHiddenRow(int officialId, float width) {
    constexpr float kRowH = 40.f;

    auto* row = CCNode::create();
    row->setContentSize({width, kRowH});
    if (auto* bg = createCardBackground({width, kRowH - 2.f})) {
        bg->setPosition({width / 2.f, kRowH / 2.f});
        row->addChild(bg, -1);
    }

    if (auto* label = CCLabelBMFont::create(
            fmt::format("{} (#{})", officialName(officialId), officialId).c_str(), "bigFont.fnt")) {
        // The "#id" suffix stays even when the name resolves, so two officials
        // with similar names are still told apart.
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({12.f, kRowH / 2.f});
        label->limitLabelWidth(220.f, 0.5f, 0.1f);
        row->addChild(label);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowH});
    menu->setID("hidden-row-menu"_spr);
    row->addChild(menu, 1);

    if (auto* spr = smallButton(tr("slot.manager.restore").c_str(), 110)) {
        auto* item = CCMenuItemExt::createSpriteExtra(
            spr, [this, officialId](CCMenuItemSpriteExtra*) {
                this->onRestoreOfficial(officialId);
            });
        item->setPosition({width - 65.f, kRowH / 2.f});
        menu->addChild(item);
    }
    return row;
}

void SlotManagerPopup::mutated() {
    // The store changed under our own menu: rebuild on the next frame, like the
    // request filters do, because this call often runs inside the touched menu.
    geode::WeakRef<SlotManagerPopup> weak(this);
    geode::Loader::get()->queueInMainThread([weak] {
        auto ref = weak.lock();
        if (!ref) return;
        auto* self = typeinfo_cast<SlotManagerPopup*>(ref.data());
        if (!self || !self->getParent()) return;
        self->rebuild();
    });
    refreshOfficialList();
    if (m_onChanged) m_onChanged();
}

void SlotManagerPopup::onAdd(CCObject*) {
    geode::WeakRef<SlotManagerPopup> weak(this);
    if (auto* editor = SlotEditorPopup::create(std::nullopt, 0, [weak] {
            auto ref = weak.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotManagerPopup*>(ref.data());
            if (!self || !self->getParent()) return;
            self->mutated();
        })) {
        editor->show();
    }
}

void SlotManagerPopup::onEditSlot(std::string const& slotId) {
    geode::WeakRef<SlotManagerPopup> weak(this);
    if (auto* editor = SlotEditorPopup::create(slotId, 0, [weak] {
            auto ref = weak.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotManagerPopup*>(ref.data());
            if (!self || !self->getParent()) return;
            self->mutated();
        })) {
        editor->show();
    }
}

void SlotManagerPopup::onTestSlot(Slot slot) {
    if (auto fresh = SlotStore::get().find(slot.id)) {
        openSlotLevel(*fresh);
    } else {
        openSlotLevel(slot);
    }
}

void SlotManagerPopup::onDeleteSlot(Slot slot) {
    std::string name = slotDisplayName(slot);
    geode::createQuickPopup(
        tr("slot.manager.delete_title").c_str(),
        fmt::format(fmt::runtime(tr("slot.manager.delete_body")), name),
        tr("general.cancel").c_str(), tr("general.ok").c_str(),
        [self = geode::Ref<SlotManagerPopup>(this), id = slot.id](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            if (!self || !self->getParent()) return;
            if (SlotStore::get().remove(id)) {
                SlotLevelCache::get().invalidate(id);
                toast(tr("slot.manager.deleted"), NotificationIcon::Success);
                self->mutated();
            }
        });
}

void SlotManagerPopup::onMoveSlot(std::string const& slotId, int delta) {
    SlotStore::get().move(slotId, delta);
    this->mutated();
}

void SlotManagerPopup::onReorder() {
    geode::WeakRef<SlotManagerPopup> weak(this);
    if (auto* popup = SlotOrderPopup::create([weak] {
            auto ref = weak.lock();
            if (!ref) return;
            auto* self = typeinfo_cast<SlotManagerPopup*>(ref.data());
            if (!self || !self->getParent()) return;
            self->mutated();
        })) {
        popup->show();
    }
}

void SlotManagerPopup::onToggleSlot(std::string const& slotId) {
    auto& store = SlotStore::get();
    if (auto slot = store.find(slotId)) {
        slot->enabled = !slot->enabled;
        store.update(*slot);
        this->mutated();
    }
}

void SlotManagerPopup::onRestoreOfficial(int officialId) {
    SlotStore::get().setOfficialHidden(officialId, false);
    toast(tr("slot.official.restored"), NotificationIcon::Success);
    this->mutated();
}

} // namespace paimon::officialslots::ui
