#include "ModuleRegistry.hpp"
#include <Geode/Geode.hpp>
#include "../../features/guide/services/PaimonGuideService.hpp"
#include "../../features/pet/services/PetManager.hpp"
#include "../../features/progressbar/services/ProgressBarManager.hpp"
#include "../../features/transitions/services/LevelEntryEffects.hpp"
#include "../../features/transitions/services/TransitionManager.hpp"
#include "../../features/cursor/services/CursorManager.hpp"
#include "../../features/icon-gradients/services/GradientAnimationManager.hpp"

// Backing::Custom modules keep their state in a manager config instead of a
// mod.json setting, so the registry needs a getter/setter pair for each.

namespace paimon::modules {

void bindCustomAccessors() {
    registerAccessor("paimbnails.pet.menu",
        [] { return PetManager::get().config().enabled; },
        [](bool on) {
            PetManager::get().config().enabled = on;
            PetManager::get().saveConfig();
        });

    registerAccessor("paimbnails.progressbar.gameplay",
        [] { return ProgressBarManager::get().config().enabled; },
        [](bool on) {
            ProgressBarManager::get().config().enabled = on;
            ProgressBarManager::get().saveConfig();
        });

    registerAccessor("paimbnails.transitions.global",
        [] { return TransitionManager::get().isEnabled(); },
        [](bool on) {
            TransitionManager::get().setEnabled(on);
            TransitionManager::get().saveConfig();
        });

    registerAccessor("paimbnails.cursortransition.global",
        [] { return CursorManager::get().config().transitionEnabled; },
        [](bool on) {
            auto& cursor = CursorManager::get();
            cursor.config().transitionEnabled = on;
            cursor.applyTransitionLive();
            cursor.saveConfig();
        });

    registerAccessor("paimbnails.cursorclick.global",
        [] { return CursorManager::get().config().clickFxEnabled; },
        [](bool on) {
            auto& cursor = CursorManager::get();
            cursor.config().clickFxEnabled = on;
            cursor.applyClickLive();
            cursor.saveConfig();
        });

    registerAccessor("paimbnails.gradientanimation.global",
        [] { return icon_gradients::GradientAnimationManager::get().isEnabled(); },
        [](bool on) { icon_gradients::GradientAnimationManager::get().setEnabled(on); });

    registerAccessor("paimbnails.levelentry.gameplay",
        [] { return paimon::transitions::getLevelEntryEffectsConfig().enabled; },
        [](bool on) {
            auto cfg = paimon::transitions::getLevelEntryEffectsConfig();
            cfg.enabled = on;
            paimon::transitions::saveLevelEntryEffectsConfig(cfg);
        });

    registerAccessor("paimbnails.guide.menu",
        [] { return paimon::guide::PaimonGuideService::get().isEnabled(); },
        [](bool on) { paimon::guide::PaimonGuideService::get().setEnabled(on); });
}

} // namespace paimon::modules

$execute {
    paimon::modules::bindCustomAccessors();
}
