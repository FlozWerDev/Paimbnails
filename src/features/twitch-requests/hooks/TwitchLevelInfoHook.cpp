// queue navigation arrows inside levelinfolayer: one on each side of the
// level name to jump to the previous/next request.

#include "../TwitchRequestManager.hpp"
#include "../services/TwitchLevelOpen.hpp"
#include "../../../framework/HookConventions.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include <string>

using namespace geode::prelude;

namespace {

constexpr ccColor3B kTwitchArrow = {190, 150, 255};

bool navigationEnabled() {
    return Mod::get()->getSettingValue<bool>("twitch-requests-enabled");
}

} // namespace

class $modify(PaimonTwitchLevelInfo, LevelInfoLayer) {
    static void onModify(auto& self) {
        paimon::hooks::veryLatePost(self, "LevelInfoLayer::init");
    }

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        addQueueArrows();
        return true;
    }

    // title has no member in the bindings: first by node id, then by
    // searching the label that shows the level name.
    CCLabelBMFont* findTitleLabel() {
        if (auto* byId = typeinfo_cast<CCLabelBMFont*>(this->getChildByID("title-label"))) {
            return byId;
        }
        if (!m_level) return nullptr;

        std::string const name{m_level->m_levelName};
        if (name.empty()) return nullptr;
        if (auto* children = this->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                auto* label = typeinfo_cast<CCLabelBMFont*>(child);
                if (label) {
                    if (auto const* text = label->getString(); text && name == text) return label;
                }
            }
        }
        return nullptr;
    }

    void addQueueArrows() {
        if (!navigationEnabled() || !m_level) return;
        if (this->getChildByID("twitch-requests-nav-menu"_spr)) return;

        auto index = paimon::twitch::indexOfRequest(m_level->m_levelID);
        if (!index) return;

        auto* title = findTitleLabel();
        if (!title || !title->getParent()) return;

        // the title may hang off another node (redesigns, other mods): convert
        // its position to layer coordinates.
        auto const world = title->getParent()->convertToWorldSpace(title->getPosition());
        auto const local = this->convertToNodeSpace(world);
        float const gap = title->getScaledContentSize().width / 2.f + 20.f;

        auto* menu = CCMenu::create();
        menu->setID("twitch-requests-nav-menu"_spr);
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 100);

        auto previous = paimon::twitch::adjacentRequestIndex(m_level->m_levelID, false);
        auto next = paimon::twitch::adjacentRequestIndex(m_level->m_levelID, true);
        makeQueueArrow(menu, {local.x - gap, local.y}, false, previous.has_value());
        makeQueueArrow(menu, {local.x + gap, local.y}, true, next.has_value());
    }

    void makeQueueArrow(CCMenu* menu, CCPoint position, bool forward, bool enabled) {
        auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png");
        if (!sprite) return;
        sprite->setScale(0.6f);
        sprite->setFlipX(forward);
        sprite->setColor(enabled ? kTwitchArrow : ccColor3B{120, 120, 130});
        if (!enabled) sprite->setOpacity(110);

        auto* button = CCMenuItemExt::createSpriteExtra(sprite,
            [levelID = m_level->m_levelID.value(), forward](CCMenuItemSpriteExtra*) {
                if (auto target = paimon::twitch::adjacentRequestIndex(levelID, forward)) {
                    paimon::twitch::playRequestAt(*target, true);
                }
            });
        button->setPosition(position);
        button->setEnabled(enabled);
        menu->addChild(button);
    }
};
