#include "../SeparateDualHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/CharacterColorPage.hpp>

using namespace geode::prelude;
using paimon::separate_dual::DualKitVault;
using paimon::separate_dual::Side;
using paimon::separate_dual::moduleEnabled;
namespace save_key = paimon::separate_dual::save_key;

namespace {

constexpr IconType kDollTypes[] = {
    IconType::Cube,
    IconType::Ship,
    IconType::Ball,
    IconType::Ufo,
    IconType::Wave,
    IconType::Robot,
    IconType::Spider,
    IconType::Swing,
};

IconType shipToggleType(int tag) {
    if (tag == 1) return IconType::Ship;
    if (tag == 8) return IconType::Jetpack;
    return IconType::Cube;
}

char const* keyForColorMode(int mode) {
    if (mode == 1) return save_key::kTrim;
    if (mode == 2) return save_key::kHalo;
    return save_key::kInk;
}

} // namespace

class $modify(PaimonSeparateDualColor, CharacterColorPage) {
    struct Fields {
        Ref<CCLabelBMFont> sideTag = nullptr;
    };

    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("CharacterColorPage::onPlayerColor", Priority::Last);
        (void)self.setHookPriorityPre("CharacterColorPage::toggleGlow", Priority::Last);
    }

    void hangSideTag(char const* text, ccColor3B color, float anchorX, float insetX) {
        auto cursor = static_cast<CCNode*>(m_cursors->objectAtIndex(0));
        m_fields->sideTag = CCLabelBMFont::create(text, "bigFont.fnt");
        m_fields->sideTag->setScale(0.3f);
        m_fields->sideTag->setAnchorPoint({anchorX, 1.f});
        m_fields->sideTag->setColor(color);
        m_fields->sideTag->setID("player-label"_spr);
        cursor->addChild(m_fields->sideTag);
        m_fields->sideTag->setPosition({insetX, cursor->getContentHeight() - 1.f});
    }

    bool init() {
        if (!moduleEnabled()) return CharacterColorPage::init();
        if (!CharacterColorPage::init()) return false;
        auto vault = DualKitVault::get();

        if (vault->sideActiveIsSecondary()) {
            size_t count = std::min<size_t>(
                std::size(kDollTypes), m_playerObjects ? m_playerObjects->count() : 0);
            for (size_t i = 0; i < count; ++i) {
                vault->dressDoll(
                    static_cast<SimplePlayer*>(m_playerObjects->objectAtIndex(i)),
                    kDollTypes[i],
                    Side::Secondary
                );
            }

            m_glowToggler->toggle(!vault->haloEnabled(Side::Secondary));
            hangSideTag("P2", {0, 255, 255}, 1.f,
                static_cast<CCNode*>(m_cursors->objectAtIndex(0))->getContentWidth() - 2.5f);
        } else {
            hangSideTag("P1", {255, 255, 0}, 0.f, 2.5f);
        }

        return true;
    }

    void toggleShip(CCObject* sender) {
        CharacterColorPage::toggleShip(sender);
        if (!moduleEnabled()) return;

        if (DualKitVault::get()->sideActiveIsSecondary()
            && (sender->getTag() == 1 || sender->getTag() == 8)) {
            auto ship = static_cast<SimplePlayer*>(
                static_cast<CCMenuItemSprite*>(sender)->getNormalImage());
            DualKitVault::get()->dressDoll(ship, shipToggleType(sender->getTag()), Side::Secondary);
        }
    }

    void updateColorMode(int p0) {
        CharacterColorPage::updateColorMode(p0);
        if (!moduleEnabled()) return;
        auto vault = DualKitVault::get();

        if (m_fields->sideTag) {
            m_fields->sideTag->removeFromParentAndCleanup(false);
            static_cast<CCNode*>(m_cursors->objectAtIndex(p0))->addChild(m_fields->sideTag);
        }

        if (vault->sideActiveIsSecondary()) {
            int ink = vault->inkOf(Side::Secondary);
            int trim = vault->trimOf(Side::Secondary);
            int halo = vault->haloOf(Side::Secondary);

            for (auto [i, sprite] : CCDictionaryExt<intptr_t, ColorChannelSprite*>(m_colorButtons)) {
                auto placeAt = [&](int cursorIdx) {
                    static_cast<CCNode*>(m_cursors->objectAtIndex(cursorIdx))->setPosition(
                        m_mainLayer->convertToNodeSpace(m_buttonMenu->convertToWorldSpace(
                            sprite->getParent()->getPosition())));
                };
                if (i == ink) placeAt(0);
                if (i == trim) placeAt(1);
                if (i == halo) placeAt(2);
            }
        }
    }

    void onPlayerColor(CCObject* sender) {
        if (!moduleEnabled()) return CharacterColorPage::onPlayerColor(sender);
        auto vault = DualKitVault::get();
        auto gm = GameManager::get();

        UnlockType unlock = m_colorMode == 0 ? UnlockType::Col1 : UnlockType::Col2;

        if (vault->sideActiveIsSecondary() && gm->isColorUnlocked(sender->getTag(), unlock)) {
            char const* key = keyForColorMode(m_colorMode);

            if (vault->load<int64_t>(key, 0) != sender->getTag()) {
                static_cast<CCNode*>(m_cursors->objectAtIndex(m_colorMode))->setPosition(
                    m_mainLayer->convertToNodeSpace(m_buttonMenu->convertToWorldSpace(
                        static_cast<CCNode*>(sender)->getPosition())));
                vault->save<int64_t>(key, sender->getTag());
            } else {
                m_delegate->showUnlockPopup(sender->getTag(), unlock);
            }

            updateIconColors();
        } else {
            CharacterColorPage::onPlayerColor(sender);
        }
    }

    void toggleGlow(CCObject* sender) {
        if (!moduleEnabled()) return CharacterColorPage::toggleGlow(sender);
        if (DualKitVault::get()->sideActiveIsSecondary()) {
            DualKitVault::get()->storeHaloEnabled(
                static_cast<CCMenuItemToggler*>(sender)->isOn());
            updateIconColors();
        } else {
            CharacterColorPage::toggleGlow(sender);
        }
    }

    void updateIconColors() {
        CharacterColorPage::updateIconColors();
        if (!moduleEnabled()) return;
        auto vault = DualKitVault::get();

        if (vault->sideActiveIsSecondary()) {
            auto gm = GameManager::get();
            for (auto* icon : CCArrayExt<SimplePlayer*>(m_playerObjects)) {
                icon->setColor(gm->colorForIdx(vault->inkOf(Side::Secondary)));
                icon->setSecondColor(gm->colorForIdx(vault->trimOf(Side::Secondary)));
                icon->enableCustomGlowColor(gm->colorForIdx(vault->haloOf(Side::Secondary)));
                icon->m_hasGlowOutline = vault->haloEnabled(Side::Secondary);
                icon->updateColors();
            }
        }
    }

    void onExit() {
        CharacterColorPage::onExit();
        if (!moduleEnabled()) return;

        if (m_fields->sideTag) {
            m_fields->sideTag->removeFromParentAndCleanup(true);
            m_fields->sideTag = nullptr;
        }
    }
};
