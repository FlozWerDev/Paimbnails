// paint layer for the cosmetic official slots: every vanilla page stays in
// place and is repainted (never rebuilt, so background/music/swipe can't desync).

#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/LevelPage.hpp>

#include <Geode/binding/BoomScrollLayer.hpp>
#include <Geode/binding/ButtonSprite.hpp>
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

// stand-ins and the coming-soon pages both use ids <= 0, so the untouched
// vanilla list is kept to tell them apart on later syncs.
constexpr char const* kVanillaLevelsKey = "official-slots-vanilla-levels"_spr;

std::optional<Slot> slotForLevel(GJGameLevel* level) {
    auto slotId = SlotLevelCache::get().slotIdForLevel(level);
    if (!slotId) return std::nullopt;
    return SlotStore::get().find(*slotId);
}

std::string levelIdentity(GJGameLevel* level) {
    if (!level) return {};
    if (auto slotId = SlotLevelCache::get().slotIdForLevel(level)) {
        return SlotStore::slotKey(*slotId);
    }
    if (isOfficialId(level->m_levelID)) return SlotStore::officialKey(level->m_levelID);
    return {};
}

void syncDots(BoomScrollLayer* scroll);

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
        vanilla = CCArray::createWithArray(scroll->m_dynamicObjects);
        scroll->setUserObject(kVanillaLevelsKey, vanilla);
    }

    auto& store = SlotStore::get();
    std::vector<std::string> desired;
    for (auto const& key : store.pageOrder()) {
        int officialId = 0;
        if (SlotStore::officialKeyId(key, officialId)) {
            if (!store.isOfficialHidden(officialId)) desired.push_back(key);
        } else if (store.pageVisible(key)) {
            desired.push_back(key);
        }
    }
    // levelselect with no page at all is outside what the game handles.
    if (desired.empty()) return;

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
            auto it = officials.find(officialId);
            if (it != officials.end()) {
                level = it->second;
            } else if (glm) {
                level = glm->getMainLevel(officialId, false);
            }
        } else if (auto slot = store.find(key.substr(2))) {
            level = SlotLevelCache::get().levelForSlot(*slot);
        }
        if (!level) continue;
        levels->addObject(level);
        keys.push_back(key);
    }
    if (levels->count() == 0) return;
    levels->addObjectsFromArray(extras);

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
    Ref<CCArray> previous = CCArray::createWithArray(scroll->m_dynamicObjects);
    scroll->m_dynamicObjects->removeAllObjects();
    scroll->m_dynamicObjects->addObjectsFromArray(levels);
    syncDots(scroll);

    scroll->m_page = target;
    scroll->instantMoveToPage(target);

    for (int i = target - 1; i <= target + 1; ++i) {
        auto* page = scroll->getPage(i);
        auto* level = scroll->m_dynamicObjects->objectAtIndex(scroll->getRelativePageForNum(i));
        if (page && level) select->updatePageWithObject(page, level);
    }
}

void syncDots(BoomScrollLayer* scroll) {
    if (!scroll || !scroll->m_dots || !scroll->m_dynamicObjects) return;
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

class $modify(PaimonOfficialSlotPage, LevelPage) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "LevelPage::updateDynamicPage");
    }

    struct Fields {
        // every node our paint added to this page. dropped and rebuilt on each
        // update because boomscrolllayer recycles pages across swipes.
        std::vector<Ref<CCNode>> m_slotNodes;
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
        LevelPage::updateDynamicPage(level);

        // drop our previous paint first: this page object may have shown a
        // different official a moment ago.
        this->clearSlotPaint();
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
            if (!slot.name.empty()) {
                m_nameLabel->setString(slot.name.c_str());
                // shrink only when too long; the vanilla update reset the
                // scale just before, so this never accumulates.
                m_nameLabel->limitLabelWidth(320.f, m_nameLabel->getScale(), 0.1f);
            }
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

        CCNode* starParent = nullptr;
        CCPoint starPos{0.f, 0.f};
        if (m_starsLabel) {
            starParent = m_starsLabel->getParent();
            starPos = m_starsLabel->getPosition();
            m_starsLabel->setVisible(false);
        }
        if (m_starsSprite) {
            if (starParent && m_starsSprite->getParent() == starParent) {
                starPos = (starPos + m_starsSprite->getPosition()) / 2.f;
            }
            m_starsSprite->setVisible(false);
        }
        if (starParent) {
            // scale 1 matches the editor preview (0.5 label, 0.8 icon).
            if (auto* badge = createStarBadge(slot.stars, 1.f)) {
                badge->setPosition(starPos);
                starParent->addChild(badge);
                this->trackSlotNode(badge);
            }
        }

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

        auto* menu = CCMenu::create();
        if (!menu) return;
        menu->setID("officialslots-menu"_spr);
        menu->ignoreAnchorPointForPosition(false);
        menu->setAnchorPoint({0.f, 0.f});

        constexpr float kGap = 6.f;
        constexpr float kH = 30.f;
        float const addW = 46.f;
        float const editW = 84.f;
        float const listW = 84.f;
        float const totalW = addW + editW + listW + kGap * 2.f;

        menu->setContentSize({totalW, kH});
        menu->setPosition({win.width - 12.f - totalW, win.height - 30.f - kH / 2.f});
        this->addChild(menu, 40);

        float x = 0.f;
        this->addTopButton(menu, "+", addW, x, [this] { this->onSlotAdd(); });
        x += addW + kGap;
        this->addTopButton(
            menu, tr("slot.manager.edit").c_str(), editW, x, [this] { this->onSlotEdit(); });
        x += editW + kGap;
        this->addTopButton(
            menu, tr("slot.manager.list").c_str(), listW, x, [this] { this->onSlotList(); });
    }

    void addTopButton(
        CCMenu* menu, char const* text, float width, float x, std::function<void()> cb
    ) {
        auto* spr = ButtonSprite::create(
            text, static_cast<int>(width), true, "bigFont.fnt", "GJ_button_01.png", 20.f, 0.5f);
        if (!spr) return;
        auto* item = CCMenuItemExt::createSpriteExtra(
            spr, [cb = std::move(cb)](CCMenuItemSpriteExtra*) { cb(); });
        if (!item) return;
        item->setPosition({x + width / 2.f, menu->getContentSize().height / 2.f});
        menu->addChild(item);
    }

    void onSlotAdd() {
        using namespace paimon::officialslots::ui;
        if (auto* editor = SlotEditorPopup::create(std::nullopt, 0, [] { refresh(); })) {
            editor->show();
        }
    }

    void onSlotEdit() {
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
        if (auto slot = paimon::officialslots::SlotStore::get().slotForOfficial(id)) {
            existing = slot->id;
        }
        if (auto* editor = SlotEditorPopup::create(existing, id, [] { refresh(); })) {
            editor->show();
        }
    }

    void onSlotList() {
        using namespace paimon::officialslots::ui;
        if (auto* manager = SlotManagerPopup::create([] { refresh(); })) {
            manager->show();
        }
    }
};
