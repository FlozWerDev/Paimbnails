#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/GJGarageLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include "../../garage-hub/GarageButtonHub.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

using namespace geode::prelude;
using paimon::separate_dual::Helper;
using paimon::separate_dual::moduleEnabled;

namespace {
constexpr int kSelectionTransitionTag = 2401;
constexpr float kSelectionTransitionDuration = 0.2f;

// GJ_2PSwapBtn.png nunca se llego a empaquetar con el mod, y sin el el boton
// salia como el cuadro rosa de textura perdida. La cadena baja al glifo de las
// dos flechas en circulo, que es el icono de intercambio de toda la vida y
// ademas llena bien el boton redondo.
CircleButtonSprite* makeSwapSprite() {
    if (auto* own = paimon::SpriteHelper::safeCreate("GJ_2PSwapBtn.png"_spr)) {
        return CircleButtonSprite::create(own, CircleBaseColor::Green, CircleBaseSize::Medium);
    }

    for (char const* frame : {"GJ_updateBtn_001.png", "edit_flipXBtn_001.png"}) {
        if (auto* glyph = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
            return CircleButtonSprite::create(glyph, CircleBaseColor::Green, CircleBaseSize::Medium);
        }
    }

    // Sin ningun sprite util, que al menos se lea de quien es el kit.
    auto* text = CCLabelBMFont::create("2P", "bigFont.fnt");
    return CircleButtonSprite::create(text, CircleBaseColor::Green, CircleBaseSize::Medium);
}
}

class $modify(PaimonSeparateDualGarage, GJGarageLayer) {
    struct Fields {
        Ref<CCSprite> arrow1 = nullptr;
        Ref<CCSprite> arrow2 = nullptr;
        Ref<SimplePlayer> player2 = nullptr;

        Ref<CCSprite> m_cursor3 = nullptr;
        Ref<CCSprite> m_cursor4 = nullptr;
        Ref<CCLabelBMFont> player1Label = nullptr;
        Ref<CCLabelBMFont> player2Label = nullptr;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "GJGarageLayer::init");
        (void)self.setHookPriorityPre("GJGarageLayer::onSelect", Priority::Replace);
        (void)self.setHookPriorityPre("GJGarageLayer::onSpecial", Priority::Replace);
    }

    CCMenu* getPageMenu() {
        if (!m_iconSelection || !m_iconSelection->m_pages) return nullptr;
        auto page = typeinfo_cast<CCNode*>(m_iconSelection->m_pages->firstObject());
        return page ? page->getChildByType<CCMenu>(0) : nullptr;
    }

    CCMenu* getSpecialPageMenu() {
        if (!m_iconSelection || m_iconType != IconType::Special) return nullptr;
        auto bar = m_iconSelection->getChildByType<ListButtonBar>(0);
        if (!bar || !bar->m_pages) return nullptr;
        auto page = typeinfo_cast<CCNode*>(bar->m_pages->firstObject());
        return page ? page->getChildByType<CCMenu>(0) : nullptr;
    }

    void placeCursor(CCSprite* cursor, CCNode* item) {
        if (!cursor) return;
        auto parent = item ? item->getParent() : nullptr;
        if (!parent) {
            cursor->setVisible(false);
            return;
        }

        cursor->setPosition(convertToNodeSpace(parent->convertToWorldSpace(item->getPosition())));
        cursor->setVisible(true);
    }

    void animateSelectionNode(CCNode* node, float scale, GLubyte opacity) {
        if (!node) return;

        node->stopActionByTag(kSelectionTransitionTag);
        auto scaleAction = CCSequence::create(
            CCEaseSineOut::create(CCScaleTo::create(
                kSelectionTransitionDuration * 0.45f,
                scale * 1.04f
            )),
            CCEaseSineInOut::create(CCScaleTo::create(
                kSelectionTransitionDuration * 0.55f,
                scale
            )),
            nullptr
        );
        auto transition = CCSpawn::create(
            scaleAction,
            CCEaseSineInOut::create(CCFadeTo::create(
                kSelectionTransitionDuration,
                opacity
            )),
            nullptr
        );
        transition->setTag(kSelectionTransitionTag);
        node->runAction(transition);
    }

    void updateSelectionVisuals(bool p2, bool animate) {
        auto updateLabel = [&](CCLabelBMFont* label, bool selected) {
            if (!label) return;
            auto scale = selected ? 0.42f : 0.34f;
            auto opacity = selected ? 255 : 150;
            if (animate) {
                animateSelectionNode(label, scale, opacity);
            } else {
                label->stopActionByTag(kSelectionTransitionTag);
                label->setScale(scale);
                label->setOpacity(opacity);
            }
        };

        auto updateArrow = [&](CCSprite* arrow, bool selected) {
            if (!arrow) return;
            if (animate) {
                arrow->setVisible(true);
                animateSelectionNode(arrow, selected ? 0.44f : 0.4f, selected ? 255 : 0);
            } else {
                arrow->stopActionByTag(kSelectionTransitionTag);
                arrow->setVisible(selected);
                arrow->setScale(0.4f);
                arrow->setOpacity(selected ? 255 : 0);
            }
        };

        updateLabel(m_fields->player1Label, !p2);
        updateLabel(m_fields->player2Label, p2);
        updateArrow(m_fields->arrow1, !p2);
        updateArrow(m_fields->arrow2, p2);

        if (m_playerObject) {
            m_playerObject->stopActionByTag(kSelectionTransitionTag);
            if (animate) {
                auto fade = CCEaseSineInOut::create(CCFadeTo::create(
                    kSelectionTransitionDuration,
                    p2 ? 205 : 255
                ));
                fade->setTag(kSelectionTransitionTag);
                m_playerObject->runAction(fade);
            } else {
                m_playerObject->setOpacity(255);
            }
        }
        if (m_fields->player2) {
            m_fields->player2->stopActionByTag(kSelectionTransitionTag);
            if (animate) {
                auto fade = CCEaseSineInOut::create(CCFadeTo::create(
                    kSelectionTransitionDuration,
                    p2 ? 255 : 205
                ));
                fade->setTag(kSelectionTransitionTag);
                m_fields->player2->runAction(fade);
            } else {
                m_fields->player2->setOpacity(255);
            }
        }
    }

    void updateCursors(bool animateSelection = false) {
        auto SDI = Helper::get();
        auto menu = getPageMenu();
        auto menu2 = getSpecialPageMenu();

        auto updateGroup = [&](bool p2, CCSprite* cursor, CCSprite* cursor2) {
            if (menu) {
                auto item = menu->getChildByTag(SDI->getIconID(m_iconType, p2));
                placeCursor(cursor, item);
            } else if (cursor) {
                cursor->setVisible(false);
            }

            if (menu2) {
                auto item = menu2->getChildByTag(SDI->getIconID(IconType::ShipFire, p2));
                placeCursor(cursor2, item);
            } else if (cursor2) {
                cursor2->setVisible(false);
            }
        };

        updateGroup(false, m_cursor1, m_cursor2);
        updateGroup(true, m_fields->m_cursor3, m_fields->m_cursor4);

        bool p2 = SDI->isP2Selected();
        updateSelectionVisuals(p2, animateSelection);

        if (m_iconType == IconType::DeathEffect) {
            if (auto page = m_iconSelection ? m_iconSelection->getChildByType<CCMenu>(0) : nullptr) {
                if (auto toggler = page->getChildByType<CCMenuItemToggler>(0)) {
                    toggler->toggle(!SDI->getDeathExplode(p2));
                }
            }
        }
    }

    void on2PToggle(CCObject* sender) {
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();
        auto node = typeinfo_cast<CCNode*>(sender);
        bool p2 = node && node->getID() == "player2-button";
        bool changed = SDI->isP2Selected() != p2;
        SDI->setP2Selected(p2);
        updateCursors(changed);
    }

    void swap2PKit(CCObject*) {
        if (!moduleEnabled()) return;
        auto GM = GameManager::get();
        auto SDI = Helper::get();
        SDI->swapSavedKitWithGame();

        SDI->setSimplePlayerInfo(m_playerObject, GM->m_playerIconType, false);
        SDI->setSimplePlayerInfo(
            m_fields->player2,
            static_cast<IconType>(SDI->getSaved<int64_t>("lastmode", 0)),
            true
        );
        updateCursors();
    }

    void onSpecial(CCObject* sender) {
        if (!moduleEnabled()) return GJGarageLayer::onSpecial(sender);
        auto SDI = Helper::get();
        if (SDI->isP2Selected()) {
            SDI->setSaved<bool>("deathexplode", static_cast<CCMenuItemToggler*>(sender)->isOn());
        } else {
            GJGarageLayer::onSpecial(sender);
        }
    }

    bool init() {
        if (!moduleEnabled()) return GJGarageLayer::init();
        auto SDI = Helper::get();
        SDI->setP2Selected(false);
        m_fields->m_cursor3 = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
        m_fields->m_cursor3->setScale(0.85f);
        m_fields->m_cursor3->setID("cursor-3");
        m_fields->m_cursor3->setVisible(false);

        m_fields->m_cursor4 = CCSprite::createWithSpriteFrameName("GJ_select_001.png");
        m_fields->m_cursor4->setScale(0.85f);
        m_fields->m_cursor4->setID("cursor-4");
        m_fields->m_cursor4->setVisible(false);

        if (!GJGarageLayer::init()) return false;

        auto GM = GameManager::get();
        auto winSize = CCDirector::get()->getWinSize();

        m_cursor1->setZOrder(101);
        m_cursor2->setZOrder(101);
        this->addChild(m_fields->m_cursor3, 101);
        this->addChild(m_fields->m_cursor4, 101);

        auto c1Label = CCLabelBMFont::create("P1", "bigFont.fnt");
        c1Label->setScale(0.3f);
        c1Label->setAnchorPoint({0.f, 1.f});
        c1Label->setColor({255, 255, 0});
        c1Label->setID("c1-player-label");
        m_cursor1->addChild(c1Label);
        c1Label->setPosition({2.5f, m_cursor1->getContentHeight() - 1.f});

        auto c2Label = CCLabelBMFont::create("P1", "bigFont.fnt");
        c2Label->setScale(0.3f);
        c2Label->setAnchorPoint({0.f, 1.f});
        c2Label->setColor({255, 255, 0});
        c2Label->setID("c2-player-label");
        m_cursor2->addChild(c2Label);
        c2Label->setPosition({2.5f, m_cursor2->getContentHeight() - 1.f});

        auto c3Label = CCLabelBMFont::create("P2", "bigFont.fnt");
        c3Label->setScale(0.3f);
        c3Label->setAnchorPoint({1.f, 1.f});
        c3Label->setColor({0, 255, 255});
        c3Label->setID("c3-player-label");
        m_fields->m_cursor3->addChild(c3Label);
        c3Label->setPosition({m_fields->m_cursor3->getContentWidth() - 2.5f, m_fields->m_cursor3->getContentHeight() - 1.f});

        auto c4Label = CCLabelBMFont::create("P2", "bigFont.fnt");
        c4Label->setScale(0.3f);
        c4Label->setAnchorPoint({1.f, 1.f});
        c4Label->setColor({0, 255, 255});
        c4Label->setID("c4-player-label");
        m_fields->m_cursor4->addChild(c4Label);
        c4Label->setPosition({m_fields->m_cursor4->getContentWidth() - 2.5f, m_fields->m_cursor4->getContentHeight() - 1.f});

        m_playerObject->setPositionX(m_playerObject->getPositionX() - winSize.width/12);

        m_fields->player2 = SimplePlayer::create(0);
        m_fields->player2->setID("player2-icon");
        m_fields->player2->setScale(1.6f);
        m_fields->player2->setPosition(m_playerObject->getPosition());
        m_fields->player2->setPositionX(m_fields->player2->getPositionX() + winSize.width/6);

        if (SDI->getSaved<int64_t>("lasttype", 0) < 90
        && SDI->getSaved<int64_t>("lastmode", 0) == 0) {
            SDI->setSaved<int64_t>("lasttype", 0);
        }
        SDI->setSimplePlayerInfo(
            m_fields->player2,
            static_cast<IconType>(SDI->getSaved<int64_t>("lastmode", 0)),
            true
        );
        this->addChild(m_fields->player2);

        auto makePlayerLabel = [](char const* text, ccColor3B color, char const* id) {
            auto label = CCLabelBMFont::create(text, "bigFont.fnt");
            label->setScale(0.35f);
            label->setColor(color);
            label->setID(id);
            return label;
        };

        m_fields->player1Label = makePlayerLabel("P1", {255, 255, 0}, "player1-label");
        m_fields->player1Label->setPosition({m_playerObject->getPositionX(), m_playerObject->getPositionY() - 30.f});
        this->addChild(m_fields->player1Label, 102);

        m_fields->player2Label = makePlayerLabel("P2", {0, 255, 255}, "player2-label");
        m_fields->player2Label->setPosition({m_fields->player2->getPositionX(), m_fields->player2->getPositionY() - 30.f});
        this->addChild(m_fields->player2Label, 102);

        auto playerMenu = CCMenu::create();
        playerMenu->setContentSize(winSize);
        playerMenu->setPosition({0, 0});
        playerMenu->setID("player-buttons-menu");
        this->addChild(playerMenu);

        auto sprite = CCSprite::create("GJ_button_01.png");
        sprite->setOpacity(0);
        auto button1 = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(PaimonSeparateDualGarage::on2PToggle));
        auto button2 = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(PaimonSeparateDualGarage::on2PToggle));

        button1->setPosition(m_playerObject->getPosition());
        button2->setPosition(m_fields->player2->getPosition());
        button1->setContentSize({70.f, 50.f});
        button1->setID("player1-button");
        button2->setContentSize({70.f, 50.f});
        button2->setID("player2-button");

        playerMenu->addChild(button1);
        playerMenu->addChild(button2);


        m_fields->arrow1 = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
        m_fields->arrow2 = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");

        m_fields->arrow1->setScale(0.4f);
        m_fields->arrow1->setPosition({m_playerObject->getPositionX() - winSize.width/12, m_playerObject->getPositionY()});
        m_fields->arrow1->setID("arrow-1");

        m_fields->arrow2->setScale(0.4f);
        m_fields->arrow2->setFlipX(true);
        m_fields->arrow2->setPosition({m_fields->player2->getPositionX() + winSize.width/12, m_fields->player2->getPositionY()});
        m_fields->arrow2->setID("arrow-2");

        auto actions1 = CCArray::create();
        actions1->addObject(CCMoveBy::create(0.5, {5, 0}));
        actions1->addObject(CCMoveBy::create(0.5, {-5, 0}));

        auto actions2 = CCArray::create();
        actions2->addObject(CCMoveBy::create(0.5, {-5, 0}));
        actions2->addObject(CCMoveBy::create(0.5, {5, 0}));

        m_fields->arrow1->runAction(CCRepeatForever::create(CCSequence::create(actions1)));
        m_fields->arrow2->runAction(CCRepeatForever::create(CCSequence::create(actions2)));

        this->addChild(m_fields->arrow1);
        this->addChild(m_fields->arrow2);


        auto swapBtn = CCMenuItemSpriteExtra::create(makeSwapSprite(), this, menu_selector(PaimonSeparateDualGarage::swap2PKit));
        swapBtn->setID("swap-2p-button");
        paimon::garage_hub::addButton(
            this, swapBtn, Localization::get().getString("garage-hub.swap-2p"), 40);

        updateCursors();

        return true;
    }

    void setupPage(int p1, IconType p2) {
        GJGarageLayer::setupPage(p1, p2);
        if (!moduleEnabled()) return;
        updateCursors();
    }

    void onSelect(CCObject* sender) {
        if (!moduleEnabled()) return GJGarageLayer::onSelect(sender);
        auto SDI = Helper::get();
        auto GM = GameManager::get();

        int n = sender->getTag();
        bool isUnlocked = GM->isIconUnlocked(n, m_iconType);
        if (m_iconType == IconType::Special)
            isUnlocked = true;

        if (SDI->isP2Selected() && isUnlocked) {
            switch (m_iconType) {
                case IconType::Cube:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 0 || SDI->getSaved<int64_t>("cube", 1) != n) {
                        SDI->setSaved<int64_t>("cube", n);
                        SDI->setSaved<int64_t>("lasttype", 0);
                        SDI->setSaved<int64_t>("lastmode", 0);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Cube);
                        return;
                    }
                    break;
                case IconType::Ship:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 1 || SDI->getSaved<int64_t>("ship", 1) != n) {
                        SDI->setSaved<int64_t>("ship", n);
                        SDI->setSaved<int64_t>("lasttype", 1);
                        SDI->setSaved<int64_t>("lastmode", 1);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Ship);
                        return;
                    }
                    break;
                case IconType::Ball:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 2 || SDI->getSaved<int64_t>("roll", 1) != n) {
                        SDI->setSaved<int64_t>("roll", n);
                        SDI->setSaved<int64_t>("lasttype", 2);
                        SDI->setSaved<int64_t>("lastmode", 2);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Ball);
                        return;
                    }
                    break;
                case IconType::Ufo:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 3 || SDI->getSaved<int64_t>("bird", 1) != n) {
                        SDI->setSaved<int64_t>("bird", n);
                        SDI->setSaved<int64_t>("lasttype", 3);
                        SDI->setSaved<int64_t>("lastmode", 3);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Bird);
                        return;
                    }
                    break;
                case IconType::Wave:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 4 || SDI->getSaved<int64_t>("dart", 1) != n) {
                        SDI->setSaved<int64_t>("dart", n);
                        SDI->setSaved<int64_t>("lasttype", 4);
                        SDI->setSaved<int64_t>("lastmode", 4);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Dart);
                        return;
                    }
                    break;
                case IconType::Robot:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 5 || SDI->getSaved<int64_t>("robot", 1) != n) {
                        SDI->setSaved<int64_t>("robot", n);
                        SDI->setSaved<int64_t>("lasttype", 5);
                        SDI->setSaved<int64_t>("lastmode", 5);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Robot);
                        return;
                    }
                    break;
                case IconType::Spider:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 6 || SDI->getSaved<int64_t>("spider", 1) != n) {
                        SDI->setSaved<int64_t>("spider", n);
                        SDI->setSaved<int64_t>("lasttype", 6);
                        SDI->setSaved<int64_t>("lastmode", 6);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Spider);
                        return;
                    }
                    break;
                case IconType::Swing:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 7 || SDI->getSaved<int64_t>("swing", 1) != n) {
                        SDI->setSaved<int64_t>("swing", n);
                        SDI->setSaved<int64_t>("lasttype", 7);
                        SDI->setSaved<int64_t>("lastmode", 7);
                        m_fields->player2->setScale(1.6f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Swing);
                        return;
                    }
                    break;
                case IconType::Jetpack:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 8 || SDI->getSaved<int64_t>("jetpack", 1) != n) {
                        SDI->setSaved<int64_t>("jetpack", n);
                        SDI->setSaved<int64_t>("lasttype", 8);
                        SDI->setSaved<int64_t>("lastmode", 8);
                        m_fields->player2->setScale(1.5f);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Jetpack);
                        return;
                    }
                    break;
                case IconType::Special:
                    if (static_cast<CCMenuItemSpriteExtra*>(sender)->m_iconType == IconType::Special) {
                        if (GM->isIconUnlocked(n, IconType::Special) && (SDI->getSaved<int64_t>("lasttype", 0) != 99 || SDI->getSaved<int64_t>("trail", 1) != n)) {
                            SDI->setSaved<int64_t>("trail", n);
                            SDI->setSaved<int64_t>("lasttype", 99);
                        } else {
                            GJGarageLayer::showUnlockPopup(n, UnlockType::Streak);
                            return;
                        }
                    } else if (static_cast<CCMenuItemSpriteExtra*>(sender)->m_iconType == IconType::ShipFire) {
                        if (GM->isIconUnlocked(n, IconType::ShipFire) && (SDI->getSaved<int64_t>("lasttype", 0) != 101 || SDI->getSaved<int64_t>("shiptrail", 1) != n)) {
                            SDI->setSaved<int64_t>("shiptrail", n);
                            SDI->setSaved<int64_t>("lasttype", 101);
                        } else {
                            GJGarageLayer::showUnlockPopup(n, UnlockType::ShipFire);
                            return;
                        }
                    }
                    break;
                case IconType::DeathEffect:
                    if (SDI->getSaved<int64_t>("lasttype", 0) != 98 || SDI->getSaved<int64_t>("death", 1) != n) {
                        SDI->setSaved<int64_t>("death", n);
                        SDI->setSaved<int64_t>("lasttype", 98);
                    } else {
                        GJGarageLayer::showUnlockPopup(n, UnlockType::Death);
                        return;
                    }
                    break;
                default:
                    break;
            }

            if (static_cast<int>(m_iconType) < 10) {
                SDI->setSimplePlayerInfo(m_fields->player2, m_iconType, true);
            }
            updateCursors();

        } else {
            GJGarageLayer::onSelect(sender);
        }
    }

    void updatePlayerColors() {
        GJGarageLayer::updatePlayerColors();
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();

        if (SDI->isP2Selected()) {
            SDI->setSimplePlayerInfo(
                m_fields->player2,
                static_cast<IconType>(SDI->getSaved<int64_t>("lastmode", 0)),
                true
            );
        }
    }
};
