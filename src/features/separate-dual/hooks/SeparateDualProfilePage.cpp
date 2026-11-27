#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/ProfilePage.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include "../../../framework/HookConventions.hpp"
#include "../../profiles/services/VanillaProfileLayout.hpp"

using namespace geode::prelude;
using paimon::separate_dual::DualKitVault;
using paimon::separate_dual::Side;
using paimon::separate_dual::moduleEnabled;

namespace {

// profile doll node id -> preview type (page layout, kept as data).
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
        if (!moduleEnabled() || !m_mainLayer) return;
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

        if (this->m_ownProfile && m_mainLayer) {
            if (auto menu = typeinfo_cast<CCMenu*>(m_mainLayer->getChildByID("player-menu"))) {
                dressProfileDolls(menu, Side::Primary);
            }
        }

        if (this->m_ownProfile && m_mainLayer && !m_fields->hasLoaded) {
            m_fields->hasLoaded = true;

            if (auto menu = m_mainLayer->getChildByID("left-menu")) {
                if (!menu->getChildByID("2p-toggler"_spr)) {
                    auto labelOff = CCLabelBMFont::create("2P", "bigFont.fnt");
                    auto sprite2POff = labelOff ? CircleButtonSprite::create(labelOff, CircleBaseColor::Green, CircleBaseSize::Medium) : nullptr;
                    auto labelOn = CCLabelBMFont::create("2P", "bigFont.fnt");
                    auto sprite2POn = labelOn ? CircleButtonSprite::create(labelOn, CircleBaseColor::Cyan, CircleBaseSize::Medium) : nullptr;

                    auto fitButton = [](CCNode* spr, float target) {
                        if (!spr) return;
                        float const cur = std::max(spr->getContentWidth(), spr->getContentHeight());
                        if (cur > 0.f) spr->setScale(target / cur);
                    };
                    fitButton(sprite2POff, 26.f);
                    fitButton(sprite2POn, 26.f);

                    if (sprite2POff && sprite2POn) {
                        if (auto toggler = CCMenuItemToggler::create(sprite2POff, sprite2POn, this, menu_selector(PaimonSeparateDualProfile::on2PToggle))) {
                            toggler->setID("2p-toggler"_spr);
                            menu->addChild(toggler);
                        }
                    }
                }
                if (paimon::settings::profiles::redesignEnabled()) {
                    menu->updateLayout();
                } else {
                    paimon::profiles::normalizeVanillaLeftMenu(this->m_mainLayer);
                }
            }
        }
    }
};
