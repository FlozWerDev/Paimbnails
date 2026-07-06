#include "LevelCellMaintenance.hpp"

namespace paimon::thumbnails::levelcell {

MaintenanceAction evaluateMaintenance(MaintenanceSnapshot const& snap) {
    if (snap.isBeingDestroyed || !snap.hasLevel || snap.levelID <= 0) {
        return MaintenanceAction::None;
    }

    if (!snap.thumbnailRequested && !snap.thumbnailApplied && !snap.thumbnailFailed &&
        snap.lastRequestedLevelID == snap.levelID) {
        return MaintenanceAction::RetryLoad;
    }

    if (snap.thumbnailApplied && !snap.spriteAlive) {
        return MaintenanceAction::RetryLoad;
    }

    if (snap.thumbnailRequested && !snap.thumbnailApplied &&
        snap.thumbnailRequestAge.count() > 1500) {
        return MaintenanceAction::RetryLoad;
    }

    if (snap.thumbnailApplied &&
        snap.currentInvalidationVersion != snap.loadedInvalidationVersion) {
        return MaintenanceAction::RetryLoad;
    }

    return MaintenanceAction::None;
}

} // namespace paimon::thumbnails::levelcell