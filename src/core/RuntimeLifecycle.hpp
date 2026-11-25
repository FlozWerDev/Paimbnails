#pragma once

#include <chrono>

namespace paimon {

bool isRuntimeShuttingDown();
void markRuntimeShuttingDown();
// Sleeps up to `duration`, returning true early if runtime shutdown begins.
bool waitForShutdownOr(std::chrono::milliseconds duration);

} // namespace paimon

void cleanupDiskCache(char const* context);
