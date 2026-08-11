// Flechas de navegacion de la cola de Twitch dentro del LevelInfoLayer: una a
// cada lado del nombre del nivel para saltar al pedido anterior / siguiente.

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
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "LevelInfoLayer::init");
    }

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        addQueueArrows();
        return true;
    }

    // El titulo no tiene miembro propio en las bindings: primero por node-id y,
    // si no esta, buscando la etiqueta que muestra el nombre del nivel.
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
                if (label && label->getString() && name == label->getString()) return label;
            }
        }
        return nullptr;
    }

    void addQueueArrows() {
        if (!navigationEnabled() || !m_level) return;
        if (this->getChildByID("twitch-requests-nav-menu"_spr)) return;

        auto& manager = paimon::twitch::TwitchRequestManager::get();
        size_t const count = manager.requestCount();
        if (count < 2) return;

        auto index = paimon::twitch::indexOfRequest(m_level->m_levelID);
        if (!index) return;

        auto* title = findTitleLabel();
        if (!title || !title->getParent()) return;

        // El titulo puede colgar de otro nodo (redisenos, otros mods): pasamos
        // su posicion a coordenadas de la capa.
        auto const world = title->getParent()->convertToWorldSpace(title->getPosition());
        auto const local = this->convertToNodeSpace(world);
        float const gap = title->getScaledContentSize().width / 2.f + 20.f;

        auto* menu = CCMenu::create();
        menu->setID("twitch-requests-nav-menu"_spr);
        menu->setPosition({0.f, 0.f});
        this->addChild(menu, 100);

        bool const hasPrevious = *index > 0;
        bool const hasNext = *index + 1 < count;
        makeQueueArrow(menu, {local.x - gap, local.y}, false, hasPrevious,
            hasPrevious ? *index - 1 : 0);
        makeQueueArrow(menu, {local.x + gap, local.y}, true, hasNext,
            hasNext ? *index + 1 : 0);
    }

    void makeQueueArrow(CCMenu* menu, CCPoint position, bool forward, bool enabled, size_t target) {
        auto* sprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png");
        if (!sprite) return;
        sprite->setScale(0.6f);
        sprite->setFlipX(forward);
        sprite->setColor(enabled ? kTwitchArrow : ccColor3B{120, 120, 130});
        if (!enabled) sprite->setOpacity(110);

        auto* button = CCMenuItemExt::createSpriteExtra(sprite,
            [target](CCMenuItemSpriteExtra*) {
                paimon::twitch::playRequestAt(target, true);
            });
        button->setPosition(position);
        button->setEnabled(enabled);
        menu->addChild(button);
    }
};
