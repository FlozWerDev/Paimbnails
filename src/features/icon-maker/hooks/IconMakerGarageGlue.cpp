#include "IconMakerGarageGlue.hpp"

#include "../services/IconApplier.hpp"
#include "../../../core/Settings.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::icon_maker::garage {

void onPlayerColorChanged(GJGarageLayer* layer) {
    if (!layer) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    // Defer one frame so it lands after GD's own re-tint of the preview.
    Ref<GJGarageLayer> ref = layer;
    Loader::get()->queueInMainThread([ref]() {
        if (paimon::isRuntimeShuttingDown() || !ref) return;
        if (auto* player = ref->m_playerObject) {
            IconApplier::get().applyExactColors(player, ref->m_selectedIconType);
        }
    });
}

}  // namespace paimon::icon_maker::garage
