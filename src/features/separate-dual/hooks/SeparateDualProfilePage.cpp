#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using paimon::separate_dual::Helper;
using paimon::separate_dual::moduleEnabled;

class $modify(PaimonSeparateDualProfile, ProfilePage) {
    struct Fields {
        bool hasLoaded = false;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ProfilePage::loadPageFromUserInfo");
    }

    void toggleShip(CCObject* sender) {
        ProfilePage::toggleShip(sender);
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();

        auto ship = static_cast<SimplePlayer*>(static_cast<CCMenuItemSprite*>(sender)->getNormalImage());

        switch (sender->getTag()) {
            case 1:
                SDI->setSimplePlayerInfo(ship, IconType::Ship, SDI->isP2Selected());
                break;
            case 8:
                SDI->setSimplePlayerInfo(ship, IconType::Jetpack, SDI->isP2Selected());
                break;
        }
    }

    SimplePlayer* getPlayer(CCNode* node) {
        if (!node) return nullptr;
        return findFirstChildRecursive<SimplePlayer>(node, [](auto) { return true; });
    }

    void on2PToggle(CCObject* sender) {
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();

        auto menu = m_mainLayer->getChildByID("player-menu");
        auto shipNode = menu ? menu->getChildByID("player-ship") : nullptr;
        auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
        if (!menu || !shipNode || !toggler) return;

        auto shipType = static_cast<IconType>(shipNode->getTag());
        auto cube = getPlayer(menu->getChildByID("player-icon"));
        auto ship = getPlayer(menu->getChildByID("player-ship"));
        auto ball = getPlayer(menu->getChildByID("player-ball"));
        auto ufo = getPlayer(menu->getChildByID("player-ufo"));
        auto wave = getPlayer(menu->getChildByID("player-wave"));
        auto robot = getPlayer(menu->getChildByID("player-robot"));
        auto spider = getPlayer(menu->getChildByID("player-spider"));
        auto swing = getPlayer(menu->getChildByID("player-swing"));
        auto jetpack = menu->getChildByID("player-jetpack") ? getPlayer(menu->getChildByID("player-jetpack")) : nullptr;
        bool p2 = !toggler->isOn();
        SDI->setP2Selected(p2);

        SDI->setSimplePlayerInfo(cube, IconType::Cube, p2);
        if (jetpack) {
            SDI->setSimplePlayerInfo(ship, IconType::Ship, p2);
            SDI->setSimplePlayerInfo(jetpack, IconType::Jetpack, p2);
        } else {
            SDI->setSimplePlayerInfo(
                ship,
                shipType == IconType::Ship ? IconType::Ship : IconType::Jetpack,
                p2
            );
        }
        SDI->setSimplePlayerInfo(ball, IconType::Ball, p2);
        SDI->setSimplePlayerInfo(ufo, IconType::Ufo, p2);
        SDI->setSimplePlayerInfo(wave, IconType::Wave, p2);
        SDI->setSimplePlayerInfo(robot, IconType::Robot, p2);
        SDI->setSimplePlayerInfo(spider, IconType::Spider, p2);
        SDI->setSimplePlayerInfo(swing, IconType::Swing, p2);
    }

    void loadPageFromUserInfo(GJUserScore* p0) {
        ProfilePage::loadPageFromUserInfo(p0);
        if (!moduleEnabled()) return;
        auto SDI = Helper::get();
        SDI->setP2Selected(false);

        if (this->m_ownProfile) {
            if (auto menu = m_mainLayer->getChildByID("player-menu")) {
                auto apply = [&](char const* id, IconType type) {
                    SDI->setSimplePlayerInfo(getPlayer(menu->getChildByID(id)), type, false);
                };

                apply("player-icon", IconType::Cube);
                apply("player-ship", IconType::Ship);
                apply("player-jetpack", IconType::Jetpack);
                apply("player-ball", IconType::Ball);
                apply("player-ufo", IconType::Ufo);
                apply("player-wave", IconType::Wave);
                apply("player-robot", IconType::Robot);
                apply("player-spider", IconType::Spider);
                apply("player-swing", IconType::Swing);
            }
        }

        if (this->m_ownProfile && !m_fields->hasLoaded) {
            m_fields->hasLoaded = true;

            if (auto menu = m_mainLayer->getChildByID("left-menu")) {
                menu->setContentHeight(menu->getContentHeight()*2);
                menu->setPositionY(menu->getPositionY() - menu->getContentHeight()/4);

                auto label = CCLabelBMFont::create("2P", "bigFont.fnt");
                auto sprite2POff = CircleButtonSprite::create(label, CircleBaseColor::Green, CircleBaseSize::Medium);
                sprite2POff->setScale(0.7f);
                auto sprite2POn = CircleButtonSprite::create(label, CircleBaseColor::Cyan, CircleBaseSize::Medium);
                sprite2POn->setScale(0.7f);

                auto toggler = CCMenuItemToggler::create(sprite2POff, sprite2POn, this, menu_selector(PaimonSeparateDualProfile::on2PToggle));
                toggler->setID("2p-toggler");
                menu->addChild(toggler);
                menu->updateLayout();
            }
        }
    }
};
