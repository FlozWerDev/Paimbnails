// Paint layer for the cosmetic official slots: every vanilla page stays in
// place and is repainted (never rebuilt, so background/music/swipe can't desync).

#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/LevelPage.hpp>

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/utils/cocos.hpp>

#include <fmt/format.h>

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

#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
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

// Pages carrying one of ours; vanilla pages are keyed by official id.
constexpr char const* kSlotPagePrefix = "paimon-slot:";

std::string pageIdentity(LevelPage* page) {
    if (!page) return {};
    std::string const tag = page->getID();
    if (tag.rfind(kSlotPagePrefix, 0) == 0) {
        return SlotStore::slotKey(tag.substr(std::strlen(kSlotPagePrefix)));
    }
    if (page->m_level && isOfficialId(page->m_level->m_levelID)) {
        return SlotStore::officialKey(page->m_level->m_levelID);
    }
    return {};
}

std::optional<Slot> slotForPage(LevelPage* page) {
    if (!page) return std::nullopt;
    std::string const tag = page->getID();
    if (tag.rfind(kSlotPagePrefix, 0) != 0) return std::nullopt;
    return SlotStore::get().find(tag.substr(std::strlen(kSlotPagePrefix)));
}

void syncDots(BoomScrollLayer* scroll);

// Reconciles the live pages with the store order: appended slots get real
// pages, hidden officials lose theirs. Runs on open and after every mutation.
void syncPages(LevelSelectLayer* select) {
    if (!select || !slotsEnabled()) return;
    auto* scroll = select->m_scrollLayer;
    if (!scroll || !scroll->m_pages) return;

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
    // LevelSelect with no page at all is outside what the game handles.
    if (desired.empty()) return;

    std::string current;
    int const pageCount = static_cast<int>(scroll->m_pages->count());
    if (scroll->m_page >= 0 && scroll->m_page < pageCount) {
        current = pageIdentity(
            typeinfo_cast<LevelPage*>(scroll->m_pages->objectAtIndex(scroll->m_page)));
    }

    for (int i = pageCount - 1; i >= 0; --i) {
        scroll->removePageWithNumber(i);
    }
    auto* glm = GameLevelManager::get();
    for (auto const& key : desired) {
        int officialId = 0;
        LevelPage* page = nullptr;
        if (SlotStore::officialKeyId(key, officialId)) {
            if (glm) page = LevelPage::create(glm->getMainLevel(officialId, false));
        } else if (auto slot = store.find(key.substr(2))) {
            page = LevelPage::create(SlotLevelCache::get().levelForSlot(*slot));
            if (page) page->setID(fmt::format("{}{}", kSlotPagePrefix, slot->id));
        }
        if (page) scroll->addPage(page);
    }
    scroll->updatePages();
    syncDots(scroll);

    int target = 0;
    for (std::size_t i = 0; i < desired.size(); ++i) {
        if (!current.empty() && desired[i] == current) {
            target = static_cast<int>(i);
            break;
        }
    }
    scroll->m_page = target;
    scroll->instantMoveToPage(target);

    for (auto* child : CCArrayExt<CCNode*>(scroll->m_pages)) {
        if (auto* page = typeinfo_cast<LevelPage*>(child)) {
            page->updateDynamicPage(page->m_level);
        }
    }
}

void syncDots(BoomScrollLayer* scroll) {
    if (!scroll || !scroll->m_dots || !scroll->m_pages) return;
    auto* dots = scroll->m_dots;
    unsigned const want = scroll->m_pages->count();
    while (dots->count() > want) {
        unsigned const last = dots->count() - 1;
        if (auto* dot = typeinfo_cast<CCNode*>(dots->objectAtIndex(last))) {
            dot->removeFromParent();
        }
        dots->removeObjectAtIndex(last);
    }
    // Clone the vanilla dot texture so the row keeps its look at any count.
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
        // Every node our paint added to this page. Dropped and rebuilt on each
        // update because BoomScrollLayer recycles pages across swipes.
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

        // Drop our previous paint first: this page object may have shown a
        // different official a moment ago.
        this->clearSlotPaint();
        if (m_levelDisplay) m_levelDisplay->setColor({255, 255, 255});
        if (m_nameLabel) m_nameLabel->setColor({255, 255, 255});

        if (!level) return;
        if (!slotsEnabled()) return;

        // Appended slots live on their own page, tagged at creation.
        if (auto slot = paimon::officialslots::slotForPage(this)) {
            if (slot->enabled) this->paintSlot(*slot);
            return;
        }

        int const id = level->m_levelID;
        if (!paimon::officialslots::isOfficialId(id)) return;

        auto& store = paimon::officialslots::SlotStore::get();
        if (auto slot = store.slotForOfficial(id)) {
            // A disabled replacement steps aside and shows the vanilla page.
            if (slot->enabled) this->paintSlot(*slot);
        }
    }

    void paintSlot(paimon::officialslots::Slot const& slot) {
        using namespace paimon::officialslots;
        using namespace paimon::officialslots::ui;

        if (m_nameLabel) {
            if (!slot.name.empty()) {
                m_nameLabel->setString(slot.name.c_str());
                // Shrink only when too long; the vanilla update reset the
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
            // Scale 1 matches the editor preview (0.5 label, 0.8 icon).
            if (auto* badge = createStarBadge(slot.stars, 1.f)) {
                badge->setPosition(starPos);
                starParent->addChild(badge);
                this->trackSlotNode(badge);
            }
        }

        // The slot owns the coin row completely: vanilla coins are always
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

        // Our own pages never reach vanilla: the stand-in level would break
        // the flows that key off the id.
        if (auto slot = paimon::officialslots::slotForPage(this)) {
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

    // Current page read at click time, so the buttons never track swipes.
    LevelPage* currentPage() {
        if (!m_scrollLayer || !m_scrollLayer->m_extendedLayer || !m_scrollLayer->m_pages) {
            return nullptr;
        }
        float const width = m_scrollLayer->getContentSize().width;
        if (width <= 0.f) return nullptr;
        int const page = static_cast<int>(
            std::round(-m_scrollLayer->m_extendedLayer->getPositionX() / width));
        if (page < 0 || page >= static_cast<int>(m_scrollLayer->m_pages->count())) {
            return nullptr;
        }
        return typeinfo_cast<LevelPage*>(m_scrollLayer->m_pages->objectAtIndex(page));
    }

    int currentOfficialId() {
        auto* page = this->currentPage();
        if (!page || !page->m_level) return -1;
        int const id = page->m_level->m_levelID;
        return paimon::officialslots::isOfficialId(id) ? id : -1;
    }

    std::string currentSlotId() {
        auto* page = this->currentPage();
        if (!page) return {};
        std::string const tag = page->getID();
        if (tag.rfind(paimon::officialslots::kSlotPagePrefix, 0) != 0) return {};
        return tag.substr(std::strlen(paimon::officialslots::kSlotPagePrefix));
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
        // A slot page edits that slot; an official edits its replacement.
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
