#pragma once

// Chequeos periodicos de salud del thumbnail en LevelCell (recovery + timeout).

#include <chrono>
#include <cstdint>

namespace paimon::thumbnails::levelcell {

enum class MaintenanceAction : uint8_t {
    None,
    RetryLoad,
};

struct MaintenanceSnapshot {
    bool isBeingDestroyed = false;
    bool thumbnailRequested = false;
    bool thumbnailApplied = false;
    bool thumbnailFailed = false;
    bool spriteAlive = false;
    bool hasLevel = false;
    int32_t levelID = 0;
    int32_t lastRequestedLevelID = 0;
    int popupSettingsVersion = 0;
    int loadedPopupSettingsVersion = 0;
    int loadedInvalidationVersion = 0;
    int currentInvalidationVersion = 0;
    std::chrono::milliseconds thumbnailRequestAge{};
};

MaintenanceAction evaluateMaintenance(MaintenanceSnapshot const& snap);

} // namespace paimon::thumbnails::levelcell