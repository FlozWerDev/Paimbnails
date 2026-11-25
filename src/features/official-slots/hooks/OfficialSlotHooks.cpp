#include "../../../framework/compat/NativeUI.hpp"
// keep the recycled vanilla pages and rebuild their source list in the user's order.

#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/LevelPage.hpp>
#include <Geode/modify/GameManager.hpp>

#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/utils/cocos.hpp>

#include "../OfficialSlots.hpp"
#include "../services/OfficialSlotStore.hpp"
#include "../services/SlotLevels.hpp"
#include "../services/SlotListRefresh.hpp"
#include "../ui/SlotEditorPopup.hpp"
#include "../ui/SlotManagerPopup.hpp"
#include "../ui/SlotVisuals.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

bool slotsEnabled() {
    return paimon::modules::isEnabled(paimon::officialslots::kModuleId);
}

std::string tr(char const* key) {
    return Localization::get().getString(key);
}

void toast(std::string const& text, NotificationIcon icon) {
    PaimonNotify::show(text, icon);
}

void refresh() {
    paimon::officialslots::refreshOfficialList();
}

} // namespace

namespace paimon::officialslots {

// keep the original list so restored officials and special pages retain their native data.
constexpr char const* kVanillaLevelsKey = "official-slots-vanilla-levels"_spr;

std::optional<Slot> slotForLevel(GJGameLevel* level) {
    auto slotId = SlotLevelCache::get().slotIdForLevel(level);
    if (!slotId) return std::nullopt;
    return SlotStore::get().find(*slotId);
}

std::string levelIdentity(GJGameLevel* level) {
    if (!level) return {};
    if (auto slotId = SlotLevelCache::get().slotIdForLevel(level)) {
        if (auto slot = SlotStore::get().find(*slotId); slot && slot->replacesOfficialId != 0) {
            return SlotStore::officialKey(slot->replacesOfficialId);
        }
        return SlotStore::slotKey(*slotId);
    }
    if (isOfficialId(level->m_levelID)) return SlotStore::officialKey(level->m_levelID);
    return {};
}

void syncDots(BoomScrollLayer* scroll);

void selectVisiblePage(LevelSelectLayer* select, int index) {
    if (!select || !select->m_scrollLayer) return;
    auto* scroll = select->m_scrollLayer;
    if (!scroll->m_dynamicObjects || scroll->m_dynamicObjects->count() == 0 ||
        !scroll->m_pages || scroll->m_pages->count() == 0) return;
    int const target = std::clamp(index, 0, static_cast<int>(scroll->m_dynamicObjects->count()) - 1);
    scroll->m_page = target;
    scroll->instantMoveToPage(target);
    for (int i = target - 1; i <= target + 1; ++i) {
        auto* page = scroll->getPage(i);
        auto* level = scroll->m_dynamicObjects->objectAtIndex(scroll->getRelativePageForNum(i));
        if (page && level) select->updatePageWithObject(page, level);
    }
}

// createWithArray deep-copies via copy(); GJGameLevel has no copyWithZone, so it
// yields nullptr entries and crashes in retain.
CCArray* shallowCopy(CCArray* src) {
    auto* out = CCArray::create();
    if (src) out->addObjectsFromArray(src);
    return out;
}

// rewrites the level list behind the scroll layer in store order: appended
// slots get an entry, hidden officials lose theirs. runs on open and after every mutation.
void syncPages(LevelSelectLayer* select) {
    if (!select || !slotsEnabled()) return;
    auto* scroll = select->m_scrollLayer;
    // levelselect recycles three pages over m_dynamicObjects. removing those pages
    // leaves getPage wrapping over a count of 0, which hung the game on open.
    if (!scroll || !scroll->m_dynamic || !scroll->m_dynamicObjects || !scroll->m_pages) return;
    if (scroll->m_pages->count() == 0 || scroll->m_dynamicObjects->count() == 0) return;

    auto* vanilla = typeinfo_cast<CCArray*>(scroll->getUserObject(kVanillaLevelsKey));
    if (!vanilla) {
        vanilla = shallowCopy(scroll->m_dynamicObjects);
        scroll->setUserObject(kVanillaLevelsKey, vanilla);
    }

    auto& store = SlotStore::get();
    auto desired = store.visiblePages();

    int const oldIndex = scroll->getRelativePageForNum(scroll->m_page);
    std::string const current = levelIdentity(
        typeinfo_cast<GJGameLevel*>(scroll->m_dynamicObjects->objectAtIndex(oldIndex)));

    std::unordered_map<int, GJGameLevel*> officials;
    auto* extras = CCArray::create();
    for (auto* obj : CCArrayExt<CCObject*>(vanilla)) {
        auto* level = typeinfo_cast<GJGameLevel*>(obj);
        if (level && isOfficialId(level->m_levelID)) {
            officials[level->m_levelID] = level;
        } else if (obj) {
            extras->addObject(obj);
        }
    }

    auto* glm = GameLevelManager::get();
    auto* levels = CCArray::create();
    std::vector<std::string> keys;
    for (auto const& key : desired) {
        int officialId = 0;
        GJGameLevel* level = nullptr;
        if (SlotStore::officialKeyId(key, officialId)) {
            if (auto replacement = store.slotForOfficial(officialId)) {
                level = SlotLevelCache::get().levelForSlot(*replacement);
            } else {
                auto it = officials.find(officialId);
                if (it != officials.end()) level = it->second;
                else if (glm) level = glm->getMainLevel(officialId, false);
            }
        } else if (auto slot = store.find(key.substr(2))) {
            level = SlotLevelCache::get().levelForSlot(*slot);
        }
        if (!level) continue;
        levels->addObject(level);
        keys.push_back(key);
    }
    levels->addObjectsFromArray(extras);
    // an empty catalogue uses the game's coming-soon page, never the old hidden list.
    if (levels->count() == 0) {
        auto* empty = GJGameLevel::create();
        if (!empty) return;
        empty->m_levelID = 0;
        levels->addObject(empty);
    }

    int target = -1;
    if (!current.empty()) {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == current) {
                target = static_cast<int>(i);
                break;
            }
        }
    }
    if (target < 0) target = std::min(oldIndex, static_cast<int>(levels->count()) - 1);

    // the recycled pages may still point at levels this swap drops.
    Ref<CCArray> previous = shallowCopy(scroll->m_dynamicObjects);
    scroll->m_dynamicObjects->removeAllObjects();
    scroll->m_dynamicObjects->addObjectsFromArray(levels);
    syncDots(scroll);

    selectVisiblePage(select, target);
}

void syncDots(BoomScrollLayer* scroll) {
    if (!scroll || !scroll->m_dots || !scroll->m_dynamicObjects) return;
    bool const visible = scroll->m_dotsVisible;
    auto* dots = scroll->m_dots;
    unsigned const want = scroll->m_dynamicObjects->count();
    while (dots->count() > want) {
        unsigned const last = dots->count() - 1;
        if (auto* dot = typeinfo_cast<CCNode*>(dots->objectAtIndex(last))) {
            dot->removeFromParent();
        }
        dots->removeObjectAtIndex(last);
    }
    // clone the vanilla dot texture so the row keeps its look at any count.
    if (dots->count() > 0) {
        if (auto* tpl = typeinfo_cast<CCSprite*>(dots->objectAtIndex(0))) {
            CCNode* parent = tpl->getParent();
            while (dots->count() < want) {
                auto* clone = CCSprite::createWithTexture(tpl->getTexture());
                if (!clone) break;
                clone->setTextureRect(tpl->getTextureRect());
                clone->setScale(tpl->getScale());
                if (parent) parent->addChild(clone);
                dots->addObject(clone);
            }
        }
    }
    scroll->updateDots(0.f);
    // cloned dots start visible even when LevelSelect replaced the row with
    // its scrollbar. Reapply the native visibility after rebuilding it.
    paimon::compat::setPageIndicatorsVisible(scroll, visible);
}

void refreshOfficialList() {
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scene = director->getRunningScene();
    if (!scene) return;

    LevelSelectLayer* select = nullptr;
    std::function<void(CCNode*)> find = [&](CCNode* node) {
        if (!node || select) return;
        if (auto* layer = typeinfo_cast<LevelSelectLayer*>(node)) {
            select = layer;
            return;
        }
        if (auto* kids = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(kids)) find(child);
        }
    };
    find(scene);
    if (!select) return;
    syncPages(select);
}

} // namespace paimon::officialslots

class $modify(PaimonOfficialSlotReturn, GameManager) {
    $override
    void returnToLastScene(GJGameLevel* level) {
        if (paimon::officialslots::returnFromSlotLevel(level)) return;
        GameManager::returnToLastScene(level);
    }
};

class $modify(PaimonOfficialSlotPage, LevelPage) {
    static void onModify(auto& self) {
        // apply the custom badges after the thumbnail hook restores vanilla children.
        paimon::hooks::veryLatePost(self, "LevelPage::updateDynamicPage");
    }

    struct Fields {
        // every node our paint added to this page. dropped and rebuilt on each
        // update because boomscrolllayer recycles pages across swipes.
        std::vector<Ref<CCNode>> m_slotNodes;
        float m_nativeNameScale = 0.f;
    };

    void trackSlotNode(CCNode* node) {
        if (node) m_fields->m_slotNodes.push_back(node);
    }

    void clearSlotPaint() {
        for (auto& node : m_fields->m_slotNodes) {
            if (node) node->removeFromParent();
        }
        m_fields->m_slotNodes.clear();
    }

    $override
    void updateDynamicPage(GJGameLevel* level) {
        bool const hadSlotPaint = !m_fields->m_slotNodes.empty();
        bool const changedLevel = !m_level || !level || m_level->m_levelID != level->m_levelID;
        this->clearSlotPaint();
        // a recycled custom page must restore the vanilla controls before updating them.
        if (hadSlotPaint) {
            if (m_difficultySprite) m_difficultySprite->setVisible(true);
            if (m_starsLabel) m_starsLabel->setVisible(true);
            if (m_starsSprite) m_starsSprite->setVisible(true);
            if (m_coins) {
                for (auto* coin : CCArrayExt<CCNode*>(m_coins)) coin->setVisible(true);
            }
        }
        LevelPage::updateDynamicPage(level);
        if (m_nameLabel && (changedLevel || m_fields->m_nativeNameScale <= 0.f)) {
            m_fields->m_nativeNameScale = m_nameLabel->getScale();
        }

        // the same page object may have shown another official a moment ago.
        if (m_levelDisplay) m_levelDisplay->setColor({255, 255, 255});
        if (m_nameLabel) m_nameLabel->setColor({255, 255, 255});

        if (!level) return;
        if (!slotsEnabled()) return;

        // appended slots arrive as their own stand-in level.
        if (auto slot = paimon::officialslots::slotForLevel(level)) {
            if (slot->enabled) this->paintSlot(*slot);
            return;
        }

        int const id = level->m_levelID;
        if (!paimon::officialslots::isOfficialId(id)) return;

        auto& store = paimon::officialslots::SlotStore::get();
        if (auto slot = store.slotForOfficial(id)) {
            // a disabled replacement steps aside and shows the vanilla page.
            if (slot->enabled) this->paintSlot(*slot);
        }
    }

    void paintSlot(paimon::officialslots::Slot const& slot) {
        using namespace paimon::officialslots;
        using namespace paimon::officialslots::ui;

        if (m_nameLabel) {
            m_nameLabel->setString(slotDisplayName(slot).c_str());
            m_nameLabel->limitLabelWidth(320.f, m_fields->m_nativeNameScale, 0.1f);
        }

        if (m_difficultySprite) {
            auto* parent = m_difficultySprite->getParent();
            CCPoint const pos = m_difficultySprite->getPosition();
            float const scale = m_difficultySprite->getScale();
            m_difficultySprite->setVisible(false);
            if (parent) {
                if (auto* badge = createDifficultyBadge(slot.difficulty, slot.tier, scale)) {
                    badge->setPosition(pos);
                    parent->addChild(badge);
                    this->trackSlotNode(badge);
                }
            }
        }

        CCNode* starParent = m_starsSprite ? m_starsSprite->getParent()
            : (m_starsLabel ? m_starsLabel->getParent() : nullptr);
        CCPoint const starPos = m_starsSprite ? m_starsSprite->getPosition()
            : (m_starsLabel ? m_starsLabel->getPosition() : CCPoint{});
        updatePageStars(m_starsLabel, m_starsSprite, slot.stars);
        auto* orbsLabel = typeinfo_cast<CCLabelBMFont*>(this->getChildByIDRecursive("orbs-label"));
        auto* orbsIcon = typeinfo_cast<CCSprite*>(this->getChildByIDRecursive("orbs-icon"));
        updatePageOrbs(orbsLabel, orbsIcon, m_level ? m_level->m_normalPercent.value() : 0);

        // the slot owns the coin row completely: vanilla coins are always
        // hidden on a replacement, ours appear only when the slot has them.
        CCNode* coinParent = nullptr;
        CCPoint coinPos{0.f, 0.f};
        float coinScale = 0.55f;
        if (m_coins) {
            bool first = true;
            for (auto* coin : CCArrayExt<CCNode*>(m_coins)) {
                if (!coin) continue;
                if (first) {
                    coinParent = coin->getParent();
                    coinPos = coin->getPosition();
                    coinScale = coin->getScale();
                    first = false;
                }
                coin->setVisible(false);
            }
        }
        if (slot.coins) {
            if (!coinParent) {
                coinParent = starParent;
                coinPos = starPos + CCPoint{0.f, -26.f};
            }
            if (coinParent) {
                if (auto* row = createCoinRow(coinScale)) {
                    row->setPosition(coinPos);
                    coinParent->addChild(row);
                    this->trackSlotNode(row);
                }
            }
        }
    }

    bool handleSlotTap() {
        if (!slotsEnabled()) return false;

        // our own pages never reach vanilla: the stand-in level would break
        // the flows that key off the id.
        if (auto slot = paimon::officialslots::slotForLevel(m_level)) {
            if (slot->enabled) paimon::officialslots::openSlotLevel(*slot);
            return true;
        }

        if (!m_level) return false;
        int const id = m_level->m_levelID;
        if (!paimon::officialslots::isOfficialId(id)) return false;

        auto& store = paimon::officialslots::SlotStore::get();
        if (auto slot = store.slotForOfficial(id)) {
            if (slot->enabled) {
                paimon::officialslots::openSlotLevel(*slot);
                return true;
            }
        }
        return false;
    }

    $override
    void onPlay(CCObject* sender) {
        if (this->handleSlotTap()) return;
        LevelPage::onPlay(sender);
    }

    $override
    void onInfo(CCObject* sender) {
        if (this->handleSlotTap()) return;
        LevelPage::onInfo(sender);
    }
};

class $modify(PaimonOfficialSlotSelect, LevelSelectLayer) {
    struct Fields {
        CCNode* m_actionsPanel = nullptr;
        CCMenu* m_actionsMenu = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelSelectLayer::init");
    }

    $override
    bool init(int p0) {
        if (!LevelSelectLayer::init(p0)) return false;
        if (slotsEnabled()) {
            this->addSlotButtons();
            paimon::officialslots::syncPages(this);
        }
        return true;
    }

    // read at click time from the level list, since the three pages are recycled.
    GJGameLevel* currentLevel() {
        auto* scroll = m_scrollLayer;
        if (!scroll || !scroll->m_extendedLayer || !scroll->m_dynamic || !scroll->m_dynamicObjects) {
            return nullptr;
        }
        if (scroll->m_dynamicObjects->count() == 0) return nullptr;
        float const width = scroll->getContentSize().width;
        if (width <= 0.f) return nullptr;
        int const page = static_cast<int>(
            std::round(-scroll->m_extendedLayer->getPositionX() / width));
        return typeinfo_cast<GJGameLevel*>(
            scroll->m_dynamicObjects->objectAtIndex(scroll->getRelativePageForNum(page)));
    }

    int currentOfficialId() {
        auto* level = this->currentLevel();
        if (!level) return -1;
        int const id = level->m_levelID;
        return paimon::officialslots::isOfficialId(id) ? id : -1;
    }

    std::string currentSlotId() {
        auto slotId = paimon::officialslots::SlotLevelCache::get().slotIdForLevel(this->currentLevel());
        return slotId ? *slotId : std::string{};
    }

    void addSlotButtons() {
        auto* director = CCDirector::get();
        if (!director) return;
        auto const win = director->getWinSize();

        constexpr float buttonWidth = 116.f;
        constexpr float gap = 6.f;
        constexpr float rowHeight = 28.f;
        constexpr float padding = 8.f;
        constexpr float menuHeight = rowHeight * 3.f + gap * 2.f;
        constexpr float panelWidth = buttonWidth + padding * 2.f;
        constexpr float panelHeight = menuHeight + padding * 2.f;

        auto* toggleMenu = CCMenu::create();
        toggleMenu->setPosition({0.f, 0.f});
        toggleMenu->setContentSize(win);
        toggleMenu->setID("officialslots-toggle-menu"_spr);
        this->addChild(toggleMenu, 41);
        auto* toggle = paimon::ui::makeFrameButton("GJ_optionsBtn_001.png", 0.55f,
            [this] { this->toggleSlotActions(); });
        if (!toggle) return;
        toggle->setID("officialslots-toggle"_spr);

        CCPoint position{win.width - 58.f, win.height - 20.f};
        if (auto* info = this->getChildByIDRecursive("info-button"); info && info->getParent()) {
            auto const bounds = info->boundingBox();
            auto const leftCenter = this->convertToNodeSpace(info->getParent()->convertToWorldSpace(
                {bounds.getMinX(), bounds.getMidY()}));
            position = CCPoint{leftCenter.x - 8.f - toggle->getScaledContentWidth() / 2.f, leftCenter.y};
        }
        float const halfWidth = toggle->getScaledContentWidth() / 2.f;
        float const halfHeight = toggle->getScaledContentHeight() / 2.f;
        position.x = std::clamp(position.x, halfWidth + 8.f, win.width - halfWidth - 8.f);
        position.y = std::clamp(position.y, halfHeight + 8.f, win.height - halfHeight - 8.f);
        toggle->setPosition(position);
        toggleMenu->addChild(toggle);

        auto* panel = CCNode::create();
        panel->setID("officialslots-actions"_spr);
        panel->setContentSize({panelWidth, panelHeight});
        panel->setPosition({std::clamp(position.x + halfWidth - panelWidth,
            8.f, win.width - panelWidth - 8.f),
            std::max(8.f, position.y - halfHeight - 8.f - panelHeight)});
        panel->addChild(paimon::ui::makeInset({panelWidth, panelHeight}, 200), -1);
        panel->setVisible(false);
        this->addChild(panel, 40);
        m_fields->m_actionsPanel = panel;

        auto* menu = CCMenu::create();
        if (!menu) return;
        menu->setID("officialslots-menu"_spr);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({0.f, 0.f});
        menu->setContentSize({buttonWidth, menuHeight});
        menu->setPosition({padding, padding});
        menu->setEnabled(false);
        panel->addChild(menu);
        m_fields->m_actionsMenu = menu;

        std::vector<CCMenuItemSpriteExtra*> buttons;
        auto addButton = [&](std::string const& text, paimon::ui::Btn skin, std::function<void()> action) {
            auto* button = paimon::ui::makeButton(text.c_str(), std::move(action),
                skin, buttonWidth, 0.65f, "bigFont.fnt");
            if (!button) return;
            menu->addChild(button);
            buttons.push_back(button);
        };
        addButton("+ " + tr("slot.manager.add"), paimon::ui::Btn::Green, [this] { this->onSlotAdd(); });
        addButton(tr("slot.manager.edit"), paimon::ui::Btn::Cyan, [this] { this->onSlotEdit(); });
        addButton(tr("slot.manager.list"), paimon::ui::Btn::Pink, [this] { this->onSlotList(); });
        paimon::ui::matchButtonLabels(buttons);
        menu->setLayout(ColumnLayout::create()->setGap(gap)->setAxisReverse(true)
            ->setAxisAlignment(AxisAlignment::Center)
            ->setCrossAxisAlignment(AxisAlignment::Center)
            ->setAutoScale(false));
    }

    void toggleSlotActions() {
        auto* panel = m_fields->m_actionsPanel;
        auto* menu = m_fields->m_actionsMenu;
        if (!panel || !menu) return;
        bool const open = !panel->isVisible();
        panel->setVisible(open);
        menu->setEnabled(open);
    }

    void closeSlotActions() {
        if (auto* panel = m_fields->m_actionsPanel) panel->setVisible(false);
        if (auto* menu = m_fields->m_actionsMenu) menu->setEnabled(false);
    }

    void onSlotAdd() {
        this->closeSlotActions();
        using namespace paimon::officialslots::ui;
        if (auto* editor = SlotEditorPopup::create(std::nullopt, 0, [] { refresh(); })) {
            editor->show();
        }
    }

    void onSlotEdit() {
        this->closeSlotActions();
        using namespace paimon::officialslots::ui;
        // a slot page edits that slot; an official edits its replacement.
        if (!this->currentSlotId().empty()) {
            if (auto* editor = SlotEditorPopup::create(this->currentSlotId(), 0, [] { refresh(); })) {
                editor->show();
            }
            return;
        }
        int const id = this->currentOfficialId();
        if (!paimon::officialslots::isOfficialId(id)) {
            toast(tr("slot.select.no_official"), NotificationIcon::Info);
            return;
        }
        std::optional<std::string> existing;
        if (auto slot = paimon::officialslots::SlotStore::get().replacementForOfficial(id)) {
            existing = slot->id;
        }
        if (auto* editor = SlotEditorPopup::create(existing, id, [] { refresh(); })) {
            editor->show();
        }
    }

    void onSlotList() {
        this->closeSlotActions();
        using namespace paimon::officialslots::ui;
        if (auto* manager = SlotManagerPopup::create([] { refresh(); })) {
            manager->show();
        }
    }
};
