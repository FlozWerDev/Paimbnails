// Paint layer for the cosmetic official slots: every vanilla page stays in
// place and is repainted (never rebuilt, so background/music/swipe can't desync).

#include <Geode/modify/LevelSelectLayer.hpp>
#include <Geode/modify/LevelPage.hpp>

#include <Geode/binding/ButtonSprite.hpp>
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

#include <cmath>
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

// Called by the editor and the manager after every mutation. Finds the live
// select layer and re-runs the page update on each LevelPage, which repaints
// through the hook below. No-op when the list is not open.
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

    std::function<void(CCNode*)> repaint = [&](CCNode* node) {
        if (!node) return;
        if (auto* page = typeinfo_cast<LevelPage*>(node)) {
            page->updateDynamicPage(page->m_level);
            return;
        }
        if (auto* kids = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(kids)) repaint(child);
        }
    };
    repaint(select);
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
        int const id = level->m_levelID;
        if (!paimon::officialslots::isOfficialId(id)) return;

        auto& store = paimon::officialslots::SlotStore::get();
        if (auto slot = store.slotForOfficial(id)) {
            // A disabled replacement steps aside and shows the vanilla page.
            if (slot->enabled) {
                this->paintSlot(*slot);
                return;
            }
        }
        if (store.isOfficialHidden(id)) this->paintHidden();
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

    void paintHidden() {
        if (m_levelDisplay) m_levelDisplay->setColor({110, 110, 120});
        if (m_nameLabel) m_nameLabel->setColor({150, 150, 160});

        auto* ribbon = CCLabelBMFont::create(
            tr("slot.official.hidden_ribbon").c_str(), "bigFont.fnt");
        if (!ribbon) return;
        ribbon->setScale(0.7f);
        ribbon->setID("officialslots-hidden-ribbon"_spr);
        if (m_levelDisplay) {
            ribbon->setPosition(m_levelDisplay->getPosition());
            if (auto* parent = m_levelDisplay->getParent()) {
                parent->addChild(ribbon, 20);
                this->trackSlotNode(ribbon);
                return;
            }
        }
        auto win = CCDirector::get()->getWinSize();
        ribbon->setPosition({win.width / 2.f, win.height / 2.f});
        this->addChild(ribbon, 20);
        this->trackSlotNode(ribbon);
    }

    // True when the tap was consumed by a slot or a hidden official.
    bool handleSlotTap() {
        if (!slotsEnabled()) return false;
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
        if (store.isOfficialHidden(id)) {
            toast(tr("slot.official.blocked"), NotificationIcon::Info);
            return true;
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
        if (slotsEnabled()) this->addSlotButtons();
        return true;
    }

    // Mirrors the vanilla 22 + 2 page cycle: indexes 0..21 are officials
    // 1..22, the rest are the empty tail pages. Read at click time so the
    // buttons never need their own page tracking.
    int currentOfficialId() {
        if (!m_scrollLayer || !m_scrollLayer->m_extendedLayer) return -1;
        float const width = m_scrollLayer->getContentSize().width;
        if (width <= 0.f) return -1;
        int const page = static_cast<int>(
            std::round(-m_scrollLayer->m_extendedLayer->getPositionX() / width));
        constexpr int kCycle = 24;
        int const idx = ((page % kCycle) + kCycle) % kCycle;
        return (idx < 22) ? idx + 1 : -1;
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
