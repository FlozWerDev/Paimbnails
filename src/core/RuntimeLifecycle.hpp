#pragma once

namespace paimon {

bool isRuntimeShuttingDown();
void markRuntimeShuttingDown();

} // namespace paimon

// limpieza de cache de disco (usada al arrancar y al salir)
void cleanupDiskCache(char const* context);