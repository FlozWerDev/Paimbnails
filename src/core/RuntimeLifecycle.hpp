#pragma once

namespace paimon {

bool isRuntimeShuttingDown();
void markRuntimeShuttingDown();

} // namespace paimon

// Disk-cache cleanup (used at startup and exit)
void cleanupDiskCache(char const* context);