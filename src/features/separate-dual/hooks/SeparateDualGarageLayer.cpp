#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include "../../garage-hub/GarageButtonHub.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using paimon::separate_dual::DualKitVault;
using paimon::separate_dual::IconSlot;
using paimon::separate_dual::LastPicked;
using paimon::separate_dual::Side;
using paimon::separate_dual::moduleEnabled;
namespace save_key = paimon::separate_dual::save_key;

namespace {

struct GaragePickRow {
    IconType page;
    IconSlot slot;
    LastPicked code;
    int mode; // lastmode to record alongside, -1 keeps the previous one
    UnlockType unlock;
    float dollScale; // second-doll scale to apply, 0 keeps the current one
};

constexpr GaragePickRow kGaragePicks[] = {
    {IconType::Cube, IconSlot::Cube, LastPicked::kLastCube, 0, UnlockType::Cube, 1.6f},
    {IconType::Ship, IconSlot::Ship, LastPicked::kLastShip, 1, UnlockType::Ship, 1.6f},
    {IconType::Ball, IconSlot::Ball, LastPicked::kLastBall, 2, UnlockType::Ball, 1.6f},
    {IconType::Ufo, IconSlot::Bird, LastPicked::kLastUfo, 3, UnlockType::Bird, 1.6f},
    {IconType::Wave, IconSlot::Dart, LastPicked::kLastWave, 4, UnlockType::Dart, 1.6f},
    {IconType::Robot, IconSlot::Robot, LastPicked::kLastRobot, 5, UnlockType::Robot, 1.6f},
    {IconType::Spider, IconSlot::Spider, LastPicked::kLastSpider, 6, UnlockType::Spider, 1.6f},
    {IconType::Swing, IconSlot::Swing, LastPicked::kLastSwing, 7, UnlockType::Swing, 1.6f},
    {IconType::Jetpack, IconSlot::Jetpack, LastPicked::kLastJetpack, 8, UnlockType::Jetpack, 1.5f},
    {IconType::Special, IconSlot::Trail, LastPicked::kLastTrail, -1, UnlockType::Streak, 0.0f},
    {IconType::ShipFire, IconSlot::ShipFire, LastPicked::kLastShipFire, -1, UnlockType::ShipFire, 0.0f},
    {IconType::DeathEffect, IconSlot::Death, LastPicked::kLastDeath, -1, UnlockType::Death, 0.0f},
};

constexpr int kSelectionTransitionTag = 2401;
constexpr float kSelectionTransitionDuration = 0.2f;

CircleButtonSprite* makeSwapSprite() {
    if (auto* own = paimon::SpriteHelper::safeCreate("paim_garage_swap2p.png"_spr)) {
        return CircleButtonSprite::create(own, CircleBaseColor::Green, CircleBaseSize::Medium);
    }

    for (char const* frame : {"GJ_updateBtn_001.png", "edit_flipXBtn_001.png"}) {
        if (auto* glyph = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
            return CircleButtonSprite::create(glyph, CircleBaseColor::Green, CircleBaseSize::Medium);
        }
    }

    auto* text = CCLabelBMFont::create("2P", "bigFont.fnt");
    return CircleButtonSprite::create(text, CircleBaseColor::Green, CircleBaseSize::Medium);
}

} // namespace

class $modify(PaimonSeparateDualGarage, GJGarageLayer) {
    struct Fields {
        Ref<SimplePlayer> secondDoll = nullptr;

        Ref<CCSprite> cursorSecond = nullptr;
        Ref<CCSprite> cursorSecondAlt = nullptr;
        Ref<CCLabelBMFont> player1Label = nullptr;
        Ref<CCLabelBMFont> player2Label = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJGarageLayer::init");
        (void)self.setHookPriorityPre("GJGarageLayer::onSelect", Priority::VeryLate);
        (void)self.setHookPriorityPre("GJGarageLayer::onSpecial", Priority::VeryLate);
    }

    CCMenu* iconPageMenu() {
        if (!m_iconSelection || !m_iconSelection->m_pages) return nullptr;
        auto page = typeinfo_cast<CCNode*>(m_iconSelection->m_pages->firstObject());
        return page ? page->getChildByType<CCMenu>(0) : nullptr;
    }

    CCMenu* trailPageMenu() {
        if (!m_iconSelection || m_iconType != IconType::Special) return nullptr;
        auto bar = m_iconSelection->getChildByType<ListButtonBar>(0);
        if (!bar || !bar->m_pages) return nullptr;
        auto page = typeinfo_cast<CCNode*>(bar->m_pages->firstObject());
        return page ? page->getChildByType<CCMenu>(0) : nullptr;
    }

    void moveCursorTo(CCSprite* cursor, CCNode* item) {
        if (!cursor) return;
        auto parent = item ? item->getParent() : nullptr;
        if (!parent) {
            cursor->setVisible(false);
            return;
        }

        cursor->setPosition(convertToNodeSpace(parent->convertToWorldSpace(item->getPosition())));
        cursor->setVisible(true);
    }

    void updateSideLabel(Side side, bool selected, bool animate) {
        auto const secondary = side == Side::Secondary;
        auto* label = secondary ? m_fields->player2Label.data() : m_fields->player1Label.data();
        auto* doll = secondary ? m_fields->secondDoll.data() : m_playerObject;
        if (!label || !doll) return;

        constexpr int labelTransitionTag = 2402;
        constexpr float duration = 0.28f;
        auto const direction = secondary ? 1.f : -1.f;
        auto const hiddenPosition = doll->getPosition() + CCPoint{direction * 18.f, 0.f};
        auto const visiblePosition = doll->getPosition() + CCPoint{direction * 34.f, 0.f};
        auto const targetPosition = selected ? visiblePosition : hiddenPosition;
        GLubyte const targetOpacity = selected ? 255 : 0;
        auto const running = label->getActionByTag(labelTransitionTag) != nullptr;

        if (!animate) {
            // icon and page refreshes must let an in-flight selection finish.
            if (running) return;
            label->setPosition(targetPosition);
            label->setOpacity(targetOpacity);
            return;
        }

        label->stopActionByTag(labelTransitionTag);
        // reverse from the current position on rapid switches; replay on a fresh click.
        if (selected && !running) {
            label->setPosition(hiddenPosition);
            label->setOpacity(0);
        }
        auto transition = CCSpawn::create(
            CCEaseSineOut::create(CCMoveTo::create(duration, targetPosition)),
            CCEaseSineInOut::create(CCFadeTo::create(duration, targetOpacity)),
            nullptr
        );
        transition->setTag(labelTransitionTag);
        label->runAction(transition);
    }

    void refreshSideEmphasis(bool secondActive, bool animate) {
        updateSideLabel(Side::Primary, !secondActive, animate);
        updateSideLabel(Side::Secondary, secondActive, animate);

        if (m_playerObject) {
            m_playerObject->stopActionByTag(kSelectionTransitionTag);
            if (animate) {
                auto fade = CCEaseSineInOut::create(CCFadeTo::create(
                    kSelectionTransitionDuration,
                    secondActive ? 205 : 255
                ));
                fade->setTag(kSelectionTransitionTag);
                m_playerObject->runAction(fade);
            } else {
                m_playerObject->setOpacity(255);
            }
        }
        if (m_fields->secondDoll) {
            m_fields->secondDoll->stopActionByTag(kSelectionTransitionTag);
            if (animate) {
                auto fade = CCEaseSineInOut::create(CCFadeTo::create(
                    kSelectionTransitionDuration,
                    secondActive ? 255 : 205
                ));
                fade->setTag(kSelectionTransitionTag);
                m_fields->secondDoll->runAction(fade);
            } else {
                m_fields->secondDoll->setOpacity(255);
            }
        }
    }

    void refreshCursors(bool animateSide = false) {
        auto vault = DualKitVault::get();
        auto menu = iconPageMenu();
        auto trailMenu = trailPageMenu();

        auto placePair = [&](bool second, CCSprite* cursor, CCSprite* altCursor) {
            Side side = second ? Side::Secondary : Side::Primary;
            if (menu) {
                moveCursorTo(cursor, menu->getChildByTag(vault->slotIconForPreview(m_iconType, side)));
            } else if (cursor) {
                cursor->setVisible(false);
            }

            if (trailMenu) {
                moveCursorTo(altCursor, trailMenu->getChildByTag(
                    vault->slotIcon(IconSlot::ShipFire, side)));
            } else if (altCursor) {
                altCursor->setVisible(false);
            }
        };

        placePair(false, m_cursor1, m_cursor2);
        placePair(true, m_fields->cursorSecond, m_fields->cursorSecondAlt);

        bool second = vault->sideActiveIsSecondary();
        refreshSideEmphasis(second, animateSide);

        if (m_iconType == IconType::DeathEffect) {
            if (auto page = m_iconSelection ? m_iconSelection->getChildByType<CCMenu>(0) : nullptr) {
                if (auto toggler = page->getChildByType<CCMenuItemToggler>(0)) {
                    toggler->toggle(!vault->burstEnabled(second ? Side::Secondary : Side::Primary));
                }
            }
        }
    }

    void on2PToggle(CCObject* sender) {
        if (!moduleEnabled()) return;
        auto node = typeinfo_cast<CCNode*>(sender);
        bool second = node && node->getID() == "player2-button"_spr;
        DualKitVault::get()->chooseSide(second);
        refreshCursors(true);
    }

    void swapSecondKit(CCObject*) {
        if (!moduleEnabled()) return;
        auto vault = DualKitVault::get();
        vault->exchangeWithGame();

        vault->dressDoll(m_playerObject, GameManager::get()->m_playerIconType, Side::Primary);
        vault->dressDoll(
            m_fields->secondDoll,
            static_cast<IconType>(vault->load<int64_t>(save_key::kLastMode, 0)),
            Side::Secondary
        );
        refreshCursors();
    }

    void onSpecial(CCObject* sender) {
        if (!moduleEnabled()) return GJGarageLayer::onSpecial(sender);
        if (DualKitVault::get()->sideActiveIsSecondary()) {
            DualKitVault::get()->storeBurstEnabled(static_cast<CCMenuItemToggler*>(sender)->isOn());
        } else {
            GJGarageLayer::onSpecial(sender);
        }
    }

    // stores a player-2 pick; false means vanilla already showed the unlock popup.
    bool storeSecondPick(IconType kind, int picked) {
        auto vault = DualKitVault::get();
        GaragePickRow const* row = nullptr;
        for (auto const& candidate : kGaragePicks) {
            if (candidate.page == kind) {
                row = &candidate;
                break;
            }
        }
        if (!row) return true;
        // one shared page with vanilla icons: unlock state still checked per button.
        if ((kind == IconType::Special || kind == IconType::ShipFire)
            && !GameManager::get()->isIconUnlocked(picked, kind)) {
            GJGarageLayer::showUnlockPopup(picked, row->unlock);
            return false;
        }
        if (vault->load<int64_t>(save_key::kLastType, 0) == static_cast<int64_t>(row->code)
            && vault->slotIcon(row->slot, Side::Secondary) == picked) {
            GJGarageLayer::showUnlockPopup(picked, row->unlock);
            return false;
        }
        vault->storeSlot(row->slot, picked);
        vault->save<int64_t>(save_key::kLastType, static_cast<int64_t>(row->code));
        if (row->mode >= 0) {
            vault->save<int64_t>(save_key::kLastMode, row->mode);
        }
        if (row->dollScale > 0.0f && m_fields->secondDoll) {
            m_fields->secondDoll->setScale(row->dollScale);
        }
        return true;
    }

    // P1/P2 tags on every cursor so each selection says whose kit it belongs to.
    void buildCursorTags() {
        auto tagCursor = [](CCSprite* cursor, char const* text, ccColor3B color,
                            float anchorX, char const* id, float insetX) {
            auto tag = CCLabelBMFont::create(text, "bigFont.fnt");
            tag->setScale(0.3f);
            tag->setAnchorPoint({anchorX, 1.f});
            tag->setColor(color);
            tag->setID(id);
            cursor->addChild(tag);
            tag->setPosition({insetX, cursor->getContentHeight() - 1.f});
        };
        tagCursor(m_cursor1, "P1", {255, 255, 0}, 0.f, "c1-player-label"_spr, 2.5f);
        tagCursor(m_cursor2, "P1", {255, 255, 0}, 0.f, "c2-player-label"_spr, 2.5f);
        tagCursor(m_fields->cursorSecond, "P2", {0, 255, 255}, 1.f, "c3-player-label"_spr,
            m_fields->cursorSecond->getContentWidth() - 2.5f);
        tagCursor(m_fields->cursorSecondAlt, "P2", {0, 255, 255}, 1.f, "c4-player-label"_spr,
            m_fields->cursorSecondAlt->getContentWidth() - 2.5f);
    }

    // moves the P1 doll left and places the P2 doll on the right.
    void buildSecondDoll() {
        auto vault = DualKitVault::get();
        auto winSize = CCDirector::get()->getWinSize();

        m_playerObject->setPositionX(m_playerObject->getPositionX() - winSize.width / 12);

        m_fields->secondDoll = SimplePlayer::create(0);
        m_fields->secondDoll->setID("player2-icon"_spr);
        m_fields->secondDoll->setScale(1.6f);
        m_fields->secondDoll->setPosition(m_playerObject->getPosition());
        m_fields->secondDoll->setPositionX(m_fields->secondDoll->getPositionX() + winSize.width / 6);

        if (vault->load<int64_t>(save_key::kLastType, 0) < 90
            && vault->load<int64_t>(save_key::kLastMode, 0) == 0) {
            vault->save<int64_t>(save_key::kLastType, 0);
        }
        vault->dressDoll(
            m_fields->secondDoll,
            static_cast<IconType>(vault->load<int64_t>(save_key::kLastMode, 0)),
            Side::Secondary
        );
        this->addChild(m_fields->secondDoll);
    }

    // labels slide outwards from the previews; the hint stays below the icon-type tabs.
    void buildSideLabels() {
        auto makeSideLabel = [](char const* text, ccColor3B color, char const* id) {
            auto label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->setScale(0.42f);
            label->setOpacity(0);
            label->setColor(color);
            label->setID(id);
            return label;
        };

        auto p1Pos = m_playerObject->getPosition();
        auto p2Pos = m_fields->secondDoll->getPosition();

        m_fields->player1Label = makeSideLabel("P1", {255, 255, 0}, "player1-label"_spr);
        m_fields->player1Label->setAnchorPoint({1.f, 0.5f});
        m_fields->player1Label->setPosition(p1Pos);
        this->addChild(m_fields->player1Label, 102);

        m_fields->player2Label = makeSideLabel("P2", {0, 255, 255}, "player2-label"_spr);
        m_fields->player2Label->setAnchorPoint({0.f, 0.5f});
        m_fields->player2Label->setPosition(p2Pos);
        this->addChild(m_fields->player2Label, 102);

        auto hint = CCLabelBMFont::create(
            Localization::get().getString("dual.hint").c_str(), "bigFont.fnt");
        hint->setScale(0.22f);
        hint->setOpacity(170);
        hint->setID("side-hint"_spr);
        hint->setPosition({(p1Pos.x + p2Pos.x) / 2.f, p1Pos.y - 64.f});
        this->addChild(hint, 102);
    }

    // invisible touch areas over each doll: tapping one picks the side being edited.
    void buildSideButtons() {
        auto playerMenu = CCMenu::create();
        playerMenu->setContentSize(CCDirector::get()->getWinSize());
        playerMenu->setPosition({0, 0});
        playerMenu->setID("player-buttons-menu"_spr);
        this->addChild(playerMenu);

        auto makeButton = [&](CCNode* doll, char const* id) {
            auto hit = CCSprite::create("GJ_button_01.png");
            hit->setOpacity(0);
            auto button = CCMenuItemSpriteExtra::create(
                hit, this, menu_selector(PaimonSeparateDualGarage::on2PToggle));
            button->setPosition(doll->getPosition());
            button->setContentSize({70.f, 50.f});
            button->setID(id);
            playerMenu->addChild(button);
        };
        makeButton(m_playerObject, "player1-button"_spr);
        makeButton(m_fields->secondDoll, "player2-button"_spr);
    }

    void onDualHelp(CCObject*) {
        auto& loc = Localization::get();
        FLAlertLayer::create(
            loc.getString("dual.help_title").c_str(),
            loc.getString("dual.help_body"),
            "OK"
        )->show();
    }

    // info button centred in the gap between the player previews.
    void buildHelpButton() {
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        menu->setID("dual-help-menu"_spr);

        auto icon = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
        icon->setScale(0.55f);
        auto button = CCMenuItemSpriteExtra::create(
            icon, this, menu_selector(PaimonSeparateDualGarage::onDualHelp));
        button->setID("dual-help-button"_spr);
        button->setPosition({
            (m_playerObject->getPositionX() + m_fields->secondDoll->getPositionX()) / 2.f,
            m_playerObject->getPositionY() - 12.f
        });
        menu->addChild(button);
        this->addChild(menu, 103);
    }

    void buildSwapButton() {
        auto swapBtn = CCMenuItemSpriteExtra::create(makeSwapSprite(), this, menu_selector(PaimonSeparateDualGarage::swapSecondKit));
        swapBtn->setID("swap-2p-button"_spr);
        paimon::garage_hub::addButton(
            this, swapBtn, Localization::get().getString("garage-hub.swap-2p"), 40);
    }

    bool init() {
        if (!moduleEnabled()) return GJGarageLayer::init();
        DualKitVault::get()->chooseSide(false);

        auto makeCursor = [](char const* id) {
            auto cursor = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
            cursor->setScale(0.85f);
            cursor->setID(id);
            cursor->setVisible(false);
            return cursor;
        };
        m_fields->cursorSecond = makeCursor("cursor-3"_spr);
        m_fields->cursorSecondAlt = makeCursor("cursor-4"_spr);

        if (!GJGarageLayer::init()) return false;

        m_cursor1->setZOrder(101);
        m_cursor2->setZOrder(101);
        this->addChild(m_fields->cursorSecond, 101);
        this->addChild(m_fields->cursorSecondAlt, 101);

        buildCursorTags();
        buildSecondDoll();
        buildSideLabels();
        buildSideButtons();
        buildHelpButton();
        buildSwapButton();

        refreshCursors();

        return true;
    }

    void setupPage(int p1, IconType p2) {
        GJGarageLayer::setupPage(p1, p2);
        if (!moduleEnabled()) return;
        refreshCursors();
    }

    void onSelect(CCObject* sender) {
        if (!moduleEnabled()) return GJGarageLayer::onSelect(sender);
        auto vault = DualKitVault::get();

        int picked = sender->getTag();
        bool unlocked = GameManager::get()->isIconUnlocked(picked, m_iconType);
        if (m_iconType == IconType::Special) unlocked = true;

        if (!vault->sideActiveIsSecondary() || !unlocked) {
            return GJGarageLayer::onSelect(sender);
        }

        IconType kind = m_iconType;
        if (m_iconType == IconType::Special) {
            kind = static_cast<CCMenuItemSpriteExtra*>(sender)->m_iconType;
        }
        if (!storeSecondPick(kind, picked)) return;

        if (static_cast<int>(m_iconType) < 10) {
            vault->dressDoll(m_fields->secondDoll, m_iconType, Side::Secondary);
        }
        refreshCursors();
    }

    void updatePlayerColors() {
        GJGarageLayer::updatePlayerColors();
        if (!moduleEnabled()) return;
        auto vault = DualKitVault::get();

        if (vault->sideActiveIsSecondary()) {
            vault->dressDoll(
                m_fields->secondDoll,
                static_cast<IconType>(vault->load<int64_t>(save_key::kLastMode, 0)),
                Side::Secondary
            );
        }
    }
};
