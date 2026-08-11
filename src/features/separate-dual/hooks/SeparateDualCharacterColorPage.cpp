#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/CharacterColorPage.hpp>

using namespace geode::prelude;
using paimon::separate_dual::Helper;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualColor, CharacterColorPage) {
    struct Fields {
        Ref<CCLabelBMFont> m_pLabel = nullptr;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("CharacterColorPage::onPlayerColor", Priority::Replace);
        (void)self.setHookPriorityPre("CharacterColorPage::toggleGlow", Priority::Replace);
    }

    bool init() {
        if (!moduleEnabled()) return CharacterColorPage::init();
        if (!CharacterColorPage::init()) return false;
        auto SDI = Helper::get();

        if (SDI->isP2Selected()) {
            auto applyP2 = [&](int index, IconType type) {
                SDI->setSimplePlayerInfo(
                    static_cast<SimplePlayer*>(m_playerObjects->objectAtIndex(index)),
                    type,
                    true
                );
            };

            applyP2(0, IconType::Cube);
            applyP2(1, IconType::Ship);
            applyP2(2, IconType::Ball);
            applyP2(3, IconType::Ufo);
            applyP2(4, IconType::Wave);
            applyP2(5, IconType::Robot);
            applyP2(6, IconType::Spider);
            applyP2(7, IconType::Swing);

            m_glowToggler->toggle(!SDI->getSaved<bool>("glow", false));

            m_fields->m_pLabel = CCLabelBMFont::create("P2", "bigFont.fnt");
            m_fields->m_pLabel->setScale(0.3f);
            m_fields->m_pLabel->setAnchorPoint({1.f, 1.f});
            m_fields->m_pLabel->setColor({0, 255, 255});
            m_fields->m_pLabel->setID("player-label");
            static_cast<CCNode*>(m_cursors->objectAtIndex(0))->addChild(m_fields->m_pLabel);
            m_fields->m_pLabel->setPosition({
                static_cast<CCNode*>(m_cursors->objectAtIndex(0))->getContentWidth() - 2.5f,
                static_cast<CCNode*>(m_cursors->objectAtIndex(0))->getContentHeight() - 1.f
            });
            
        } else {
            m_fields->m_pLabel = CCLabelBMFont::create("P1", "bigFont.fnt");
            m_fields->m_pLabel->setScale(0.3f);
            m_fields->m_pLabel->setAnchorPoint({0.f, 1.f});
            m_fields->m_pLabel->setColor({255, 255, 0});
            m_fields->m_pLabel->setID("player-label");
            static_cast<CCNode*>(m_cursors->objectAtIndex(0))->addChild(m_fields->m_pLabel);
            m_fields->m_pLabel->setPosition({
                2.5f,
                static_cast<CCNode*>(m_cursors->objectAtIndex(0))->getContentHeight() - 1.f
            });
        }

        return true;
    }

    void toggleShip(CCObject* sender) {
        CharacterColorPage::toggleShip(sender);
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();

        if (SDI->isP2Selected()) {
            auto ship = static_cast<SimplePlayer*>(static_cast<CCMenuItemSprite*>(sender)->getNormalImage());

            switch (sender->getTag()) {
                case 1:
                    SDI->setSimplePlayerInfo(ship, IconType::Ship, true);
                    break;
                case 8:
                    SDI->setSimplePlayerInfo(ship, IconType::Jetpack, true);
                    break;
            }
        }
    }


    void updateColorMode(int p0) {
        CharacterColorPage::updateColorMode(p0);
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();

        if (m_fields->m_pLabel) {
            m_fields->m_pLabel->removeFromParentAndCleanup(false);
            static_cast<CCNode*>(m_cursors->objectAtIndex(p0))->addChild(m_fields->m_pLabel);
        }

        if (SDI->isP2Selected()) {
            auto color1 = SDI->getSaved<int64_t>("color1", 0);
            auto color2 = SDI->getSaved<int64_t>("color2", 0);
            auto colorglow = SDI->getSaved<int64_t>("colorglow", 0);
            if (colorglow == -1) colorglow = color2;

            for (auto [i, sprite] : CCDictionaryExt<intptr_t, ColorChannelSprite*>(m_colorButtons)) {
                if (i == color1) {
                    static_cast<CCNode*>(m_cursors->objectAtIndex(0))->setPosition(m_mainLayer->convertToNodeSpace(
                        m_buttonMenu->convertToWorldSpace(sprite->getParent()->getPosition())
                    ));
                }
                if (i == color2) {
                    static_cast<CCNode*>(m_cursors->objectAtIndex(1))->setPosition(m_mainLayer->convertToNodeSpace(
                        m_buttonMenu->convertToWorldSpace(sprite->getParent()->getPosition())
                    ));
                }
                if (i == colorglow) {
                    static_cast<CCNode*>(m_cursors->objectAtIndex(2))->setPosition(m_mainLayer->convertToNodeSpace(
                        m_buttonMenu->convertToWorldSpace(sprite->getParent()->getPosition())
                    ));
                }
            }
        }
    }

    void onPlayerColor(CCObject* sender) {
        if (!moduleEnabled()) return CharacterColorPage::onPlayerColor(sender);
        auto SDI = Helper::get();
        UnlockType ut;
        auto GM = GameManager::get();

        switch (m_colorMode) {
            case 0:
                ut = UnlockType::Col1;
                break;
            default:
                ut = UnlockType::Col2;
                break;
        }

        if (SDI->isP2Selected() && GM->isColorUnlocked(sender->getTag(), ut)) {
            auto colorKey = "";
            switch (m_colorMode) {
                case 0:
                    colorKey = "color1";
                    break;
                case 1:
                    colorKey = "color2";
                    break;
                case 2:
                    colorKey = "colorglow";
                    break;
            }

            if (SDI->getSaved<int64_t>(colorKey, 0) != sender->getTag()) {
                static_cast<CCNode*>(m_cursors->objectAtIndex(m_colorMode))->setPosition(m_mainLayer->convertToNodeSpace(
                    m_buttonMenu->convertToWorldSpace(static_cast<CCNode*>(sender)->getPosition())
                ));
                SDI->setSaved<int64_t>(colorKey, sender->getTag());
            } else {
                m_delegate->showUnlockPopup(sender->getTag(), ut);
            }

            updateIconColors();
        } else {
            CharacterColorPage::onPlayerColor(sender);
        }
    }

    void toggleGlow(CCObject* sender) {
        if (!moduleEnabled()) return CharacterColorPage::toggleGlow(sender);
        auto SDI = Helper::get();
        if (SDI->isP2Selected()) {
            SDI->setSaved<bool>("glow", static_cast<CCMenuItemToggler*>(sender)->isOn());
            updateIconColors();
        } else {
            CharacterColorPage::toggleGlow(sender);
        }
    }

    void updateIconColors() {
        CharacterColorPage::updateIconColors();
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();

        if (SDI->isP2Selected()) {
            auto GM = GameManager::get();
            for (auto* icon : CCArrayExt<SimplePlayer*>(m_playerObjects)) {
                icon->setColor(GM->colorForIdx(SDI->getSaved<int64_t>("color1", 0)));
                int color2 = SDI->getSaved<int64_t>("color2", 0);
                int colorglow = SDI->getSaved<int64_t>("colorglow", 0);
                icon->setSecondColor(GM->colorForIdx(color2));
                if (colorglow == -1)
                  icon->enableCustomGlowColor(GM->colorForIdx(color2));
                else 
                  icon->enableCustomGlowColor(GM->colorForIdx(colorglow));
                icon->m_hasGlowOutline = SDI->getSaved<bool>("glow", false);
                icon->updateColors();
            }
        }
    }

    
    void onExit() {
        CharacterColorPage::onExit();
        if (!moduleEnabled()) return;

        if (m_fields->m_pLabel) {
            m_fields->m_pLabel->removeFromParentAndCleanup(true);
            m_fields->m_pLabel = nullptr;
        }
    }
};
