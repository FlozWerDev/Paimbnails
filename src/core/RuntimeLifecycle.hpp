#pragma once

namespace paimon {

bool isRuntimeShuttingDown();
void markRuntimeShuttingDown();

} // namespace paimon

void cleanupDiskCache(char const* context);
