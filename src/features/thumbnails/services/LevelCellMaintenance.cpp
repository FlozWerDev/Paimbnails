#include "LevelCellMaintenance.hpp"

namespace paimon::thumbnails::levelcell {

MaintenanceAction evaluateMaintenance(MaintenanceSnapshot const& snap) {
    if (snap.isBeingDestroyed || !snap.hasLevel || snap.levelID <= 0) {
        return MaintenanceAction::None;
    }

    // Callback perdido tras onExit transitorio: re-pedir sin martillar failedCache.
    if (!snap.thumbnailRequested && !snap.thumbnailApplied && !snap.thumbnailFailed &&
        snap.lastRequestedLevelID == snap.levelID) {
        return MaintenanceAction::RetryLoad;
    }

    // Sprite desaparecio del arbol pese a thumbnailApplied.
    if (snap.thumbnailApplied && !snap.spriteAlive) {
        return MaintenanceAction::RetryLoad;
    }

    // Timeout de request colgada (>1.5s).
    if (snap.thumbnailRequested && !snap.thumbnailApplied &&
        snap.thumbnailRequestAge.count() > 1500) {
        return MaintenanceAction::RetryLoad;
    }

    // Invalidacion remota detectada en tick de mantenimiento.
    if (snap.thumbnailApplied &&
        snap.currentInvalidationVersion != snap.loadedInvalidationVersion) {
        return MaintenanceAction::RetryLoad;
    }

    return MaintenanceAction::None;
}

} // namespace paimon::thumbnails::levelcell