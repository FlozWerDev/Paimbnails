#include "SlotOrderPopup.hpp"

#include "SlotVisuals.hpp"
#include "../services/OfficialSlotStore.hpp"
#include "../services/SlotListRefresh.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/loader/Loader.hpp>

#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::officialslots::ui {

namespace {

constexpr float kWidth = 380.f;
constexpr float kHeight = 300.f;
constexpr float kRowH = 30.f;
constexpr float kRowGap = 4.f;
constexpr float kListY = 56.f;

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
    return Localization::get().getString("slot.level.unnamed");
}

} // namespace

SlotOrderPopup* SlotOrderPopup::create(std::function<void()> onChanged) {
    auto* ret = new SlotOrderPopup();
    if (ret && ret->init(std::move(onChanged))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SlotOrderPopup::init(std::function<void()> onChanged) {
    if (!Popup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);
    m_onChanged = std::move(onChanged);
    auto& loc = Localization::get();
    this->setTitle(loc.getString("slot.order.title"));
    this->buildList();
    this->buildControls();
    auto const& order = SlotStore::get().pageOrder();
    if (!order.empty()) m_selected = order.front();
    this->rebuild();
    return true;
}

void SlotOrderPopup::buildList() {
    float const scrollH = kHeight - kListY - 60.f;
    float const scrollW = kWidth - 30.f;
    if (auto* panel = CCScale9Sprite::create("GJ_square02.png")) {
        panel->setContentSize({kWidth - 20.f, scrollH + 6.f});
        panel->setPosition({kWidth / 2.f, kListY + scrollH / 2.f});
        panel->setOpacity(220);
        panel->setID("order-list-bg"_spr);
        m_mainLayer->addChild(panel, 1);
    }

    m_scroll = ScrollLayer::create({scrollW, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({15.f, kListY});
    m_scroll->m_contentLayer->setID("order-scroll-content"_spr);
    m_mainLayer->addChild(m_scroll, 2);
}

void SlotOrderPopup::buildControls() {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kWidth, kHeight});
    menu->setID("order-controls-menu"_spr);
    m_mainLayer->addChild(menu, 3);

    // same glyphs as the manager rows, plus jumps to either end.
    std::vector<std::tuple<char const*, int, int>> defs = {
        {"|^", 40, 0},
        {"^", 36, 1},
        {"v", 36, 2},
        {"v|", 40, 3},
    };
    float totalW = 0.f;
    for (auto const& [text, w, action] : defs) totalW += w + 8.f;
    totalW -= 8.f;
    float x = (kWidth - totalW) / 2.f;
    for (auto const& [text, w, action] : defs) {
        auto* spr = ButtonSprite::create(text, w, true, "bigFont.fnt", "GJ_button_02.png", 18.f, 0.45f);
        if (!spr) continue;
        auto* item = CCMenuItemExt::createSpriteExtra(
            spr, [this, action](CCMenuItemSpriteExtra*) {
                switch (action) {
                    case 0: this->onJumpTop(); break;
                    case 1: this->onStep(-1); break;
                    case 2: this->onStep(1); break;
                    default: this->onJumpBottom(); break;
                }
            });
        if (!item) continue;
        item->setPosition({x + w / 2.f, 26.f});
        menu->addChild(item);
        x += w + 8.f;
    }
}

void SlotOrderPopup::rebuild() {
    if (!m_scroll) return;
    m_scroll->m_contentLayer->removeAllChildren();

    auto& store = SlotStore::get();
    auto const& order = store.pageOrder();
    if (!order.empty() &&
        std::find(order.begin(), order.end(), m_selected) == order.end()) {
        m_selected = order.front();
    }

    float const rowW = m_scroll->getContentSize().width - 4.f;
    std::vector<CCNode*> rows;
    int index = 0;
    for (auto const& key : order) {
        int officialId = 0;
        std::string title;
        bool dimmed = false;
        if (SlotStore::officialKeyId(key, officialId)) {
            if (auto slot = store.slotForOfficial(officialId)) {
                title = slotDisplayName(*slot);
            } else {
                title = officialName(officialId);
            }
            dimmed = store.isOfficialHidden(officialId);
        } else if (auto slot = store.find(key.substr(2))) {
            title = slotDisplayName(*slot);
            dimmed = !slot->enabled;
        } else {
            continue;
        }
        if (auto* row = this->buildRow(key, index, title, dimmed, rowW, key == m_selected)) {
            rows.push_back(row);
        }
        ++index;
    }

    float totalH = 0.f;
    for (auto* row : rows) totalH += row->getContentSize().height + kRowGap;
    if (!rows.empty()) totalH -= kRowGap;
    totalH = std::max(totalH, m_scroll->getContentSize().height);

    float y = totalH;
    for (auto* row : rows) {
        float const h = row->getContentSize().height;
        y -= h;
        row->setPosition({2.f, y});
        m_scroll->m_contentLayer->addChild(row);
        y -= kRowGap;
    }
    m_scroll->m_contentLayer->setContentHeight(totalH);
    // moves keep the count, so the offset stays valid; only added or removed
    // rows (done from the manager behind) snap back to the top.
    if (rows.size() != m_rows) m_scroll->scrollToTop();
    m_rows = rows.size();
}

CCNode* SlotOrderPopup::buildRow(
    std::string const& key, int index, std::string const& title,
    bool dimmed, float width, bool selected
) {
    auto* row = CCNode::create();
    row->setContentSize({width, kRowH});

    auto* bg = createCardBackground({width, kRowH - 2.f});

    ccColor3B numColor = selected ? ccColor3B{140, 255, 160} : ccColor3B{170, 180, 200};
    if (auto* num = CCLabelBMFont::create(fmt::format("#{}", index + 1).c_str(), "bigFont.fnt")) {
        num->setAnchorPoint({0.f, 0.5f});
        num->setPosition({10.f, kRowH / 2.f});
        num->setScale(0.42f);
        num->setColor(numColor);
        row->addChild(num, 2);
    }

    if (auto* nameLbl = CCLabelBMFont::create(title.c_str(), "bigFont.fnt")) {
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({56.f, kRowH / 2.f});
        nameLbl->setScale(0.42f);
        nameLbl->limitLabelWidth(width - 68.f, 0.42f, 0.1f);
        // dimmed rows keep their layout; the grey label says hidden/off.
        if (dimmed) nameLbl->setColor({130, 130, 145});
        row->addChild(nameLbl, 2);
    }

    if (bg) {
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize({width, kRowH});
        menu->setID("order-row-menu"_spr);
        row->addChild(menu, 1);

        if (auto* item = CCMenuItemExt::createSpriteExtra(
                bg, [this, key](CCMenuItemSpriteExtra*) {
                    this->onSelectSlot(key);
                })) {
            item->setPosition({width / 2.f, kRowH / 2.f});
            menu->addChild(item);
        }
    }
    return row;
}

void SlotOrderPopup::onSelectSlot(std::string const& key) {
    if (m_selected == key) return;
    m_selected = key;
    this->refresh();
}

void SlotOrderPopup::onStep(int delta) {
    if (m_selected.empty()) return;
    SlotStore::get().movePage(m_selected, delta);
    this->mutated();
}

void SlotOrderPopup::onJumpTop() {
    if (m_selected.empty()) return;
    SlotStore::get().movePageTo(m_selected, 0);
    this->mutated();
}

void SlotOrderPopup::onJumpBottom() {
    if (m_selected.empty()) return;
    auto& store = SlotStore::get();
    if (store.pageOrder().empty()) return;
    store.movePageTo(m_selected, store.pageOrder().size() - 1);
    this->mutated();
}

void SlotOrderPopup::refresh() {
    // same as the manager: rebuild on the next frame, because this often runs
    // inside the menu being rebuilt.
    geode::WeakRef<SlotOrderPopup> weak(this);
    geode::Loader::get()->queueInMainThread([weak] {
        auto ref = weak.lock();
        if (!ref) return;
        auto* self = typeinfo_cast<SlotOrderPopup*>(ref.data());
        if (!self || !self->getParent()) return;
        self->rebuild();
    });
}

void SlotOrderPopup::mutated() {
    this->refresh();
    refreshOfficialList();
    if (m_onChanged) m_onChanged();
}

} // namespace paimon::officialslots::ui
