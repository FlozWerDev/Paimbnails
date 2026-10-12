#include "utils/MainThread.hpp"
#include "utils/Debug.hpp"
#include "framework/HookConventions.hpp"

$on_mod(Loaded) {
    paimon::captureMainThread();
    if (PaimonDebug::isEnabled()) paimon::hooks::logRegisteredHooks();
}
