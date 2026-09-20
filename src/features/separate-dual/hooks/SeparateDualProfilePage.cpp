#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using paimon::separate_dual::DualKitVault;
using paimon::separate_dual::Side;
using paimon::separate_dual::moduleEnabled;

namespace {

// Profile doll node id -> preview type (page layout, kept as data).
struct ProfileDollRow {
    char const* nodeId;
    IconType type;
};

constexpr ProfileDollRow kProfileDolls[] = {
    {"player-icon", IconType::Cube},
    {"player-ship", IconType::Ship},
    {"player-jetpack", IconType::Jetpack},
    {"player-ball", IconType::Ball},
    {"player-ufo", IconType::Ufo},
    {"player-wave", IconType::Wave},
    {"player-robot", IconType::Robot},
    {"player-spider", IconType::Spider},
    {"player-swing", IconType::Swing},
};

} // namespace

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

        if (sender->getTag() == 1 || sender->getTag() == 8) {
            auto ship = static_cast<SimplePlayer*>(
                static_cast<CCMenuItemSprite*>(sender)->getNormalImage());
            auto vault = DualKitVault::get();
            vault->dressDoll(
                ship,
                sender->getTag() == 1 ? IconType::Ship : IconType::Jetpack,
                vault->sideActiveIsSecondary() ? Side::Secondary : Side::Primary);
        }
    }

    SimplePlayer* firstDoll(CCNode* node) {
        if (!node) return nullptr;
        return findFirstChildRecursive<SimplePlayer>(node, [](auto) { return true; });
    }

    void dressProfileDolls(CCMenu* menu, Side side) {
        auto vault = DualKitVault::get();
        for (auto const& row : kProfileDolls) {
            vault->dressDoll(firstDoll(menu->getChildByID(row.nodeId)), row.type, side);
        }
    }

    void on2PToggle(CCObject* sender) {
        if (!moduleEnabled()) return;
        auto vault = DualKitVault::get();

        auto menu = m_mainLayer->getChildByID("player-menu");
        auto shipNode = menu ? menu->getChildByID("player-ship") : nullptr;
        auto toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
        if (!menu || !shipNode || !toggler) return;

        auto shipType = static_cast<IconType>(shipNode->getTag());
        bool second = !toggler->isOn();
        vault->chooseSide(second);
        Side side = second ? Side::Secondary : Side::Primary;

        auto cube = firstDoll(menu->getChildByID("player-icon"));
        auto ship = firstDoll(menu->getChildByID("player-ship"));
        auto jetpackNode = menu->getChildByID("player-jetpack");
        vault->dressDoll(cube, IconType::Cube, side);
        if (jetpackNode) {
            vault->dressDoll(ship, IconType::Ship, side);
            vault->dressDoll(firstDoll(jetpackNode), IconType::Jetpack, side);
        } else {
            vault->dressDoll(
                ship,
                shipType == IconType::Ship ? IconType::Ship : IconType::Jetpack,
                side
            );
        }
        for (auto const& row : kProfileDolls) {
            if (row.type == IconType::Cube || row.type == IconType::Ship
                || row.type == IconType::Jetpack) {
                continue;
            }
            vault->dressDoll(firstDoll(menu->getChildByID(row.nodeId)), row.type, side);
        }
    }

    void loadPageFromUserInfo(GJUserScore* p0) {
        ProfilePage::loadPageFromUserInfo(p0);
        if (!moduleEnabled()) return;
        auto vault = DualKitVault::get();
        vault->chooseSide(false);

        if (this->m_ownProfile) {
            if (auto menu = typeinfo_cast<CCMenu*>(m_mainLayer->getChildByID("player-menu"))) {
                dressProfileDolls(menu, Side::Primary);
            }
        }

        if (this->m_ownProfile && !m_fields->hasLoaded) {
            m_fields->hasLoaded = true;

            if (auto menu = m_mainLayer->getChildByID("left-menu")) {
                menu->setContentHeight(menu->getContentHeight() * 2);
                menu->setPositionY(menu->getPositionY() - menu->getContentHeight() / 4);

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
