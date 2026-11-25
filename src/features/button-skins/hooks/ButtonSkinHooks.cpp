#include "../services/ButtonSkinRuntime.hpp"
#include "../../quick-hub/services/QuickButtonSfx.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCMenuItemSpriteExtra.hpp>
#include <Geode/modify/CCMenuItemToggler.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/CCLayer.hpp>
#include <Geode/ui/SceneEvent.hpp>

#include <optional>

using namespace geode::prelude;
namespace skins = paimon::buttonskin;
namespace quickhub = paimon::quickhub;

namespace {

// a toggler activates its inner buttons: only the outermost press owns the sound.
int s_activateDepth = 0;

template <class Fn>
void activateWithSkinSound(CCMenuItem* item, skins::SkinSound const& sound, Fn&& original) {
    // the quick hub already armed its own sound for this press.
    if (s_activateDepth > 0 || quickhub::isQuickButtonSfxSuppressArmed()) {
        original();
        return;
    }
    // the callback may close the popup that owns the button.
    Ref<CCMenuItem> keep = item;
    ++s_activateDepth;
    quickhub::beginQuickButtonSfxSuppress();
    original();
    (void)quickhub::consumeQuickButtonSfxSuppress();
    quickhub::clearQuickButtonSfxSuppress();
    --s_activateDepth;
    if (sound.custom) quickhub::playQuickButtonSfx(sound.sfx);
}

} // namespace

class $modify(PaimonButtonSkinSpriteSfx, CCMenuItemSpriteExtra) {
    $override void activate() {
        auto sound = skins::ButtonSkinRuntime::get().soundFor(this);
        if (!sound) {
            CCMenuItemSpriteExtra::activate();
            return;
        }
        activateWithSkinSound(this, *sound, [this] { this->CCMenuItemSpriteExtra::activate(); });
    }
};

class $modify(PaimonButtonSkinTogglerSfx, CCMenuItemToggler) {
    $override void activate() {
        auto& runtime = skins::ButtonSkinRuntime::get();
        auto sound = runtime.soundFor(this);
        // the skin sits on the state the player sees.
        if (!sound) sound = runtime.soundFor(m_toggled ? m_onButton : m_offButton);
        if (!sound) {
            CCMenuItemToggler::activate();
            return;
        }
        activateWithSkinSound(this, *sound, [this] { this->CCMenuItemToggler::activate(); });
    }
};

// MenuLayer's modify bindings do not declare onEnter, so an override on MenuLayer never hooks.
// the hook goes on CCLayer, like PauseLayer's lifecycle hook, and only menus are handled here.
class $modify(PaimonButtonSkinEnter, CCLayer) {
    void onEnter() {
        CCLayer::onEnter();
        auto* menu = typeinfo_cast<MenuLayer*>(this);
        if (!menu || paimon::isRuntimeShuttingDown()) return;
        // the menu's buttons live in its scene: dress that scene before its first frame.
        skins::ButtonSkinRuntime::get().dressEntered(typeinfo_cast<CCScene*>(menu->getParent()));
    }
};

$execute {
    // fires before the switch, so the first frame of the new scene is already dressed.
    SceneEvent().listen(+[](CCScene* scene) -> bool {
        if (!paimon::isRuntimeShuttingDown()) skins::ButtonSkinRuntime::get().onSceneSwitch(scene);
        return false;
    }).leak();
}
